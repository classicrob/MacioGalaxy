// petari-level: the agent level-authoring CLI (docs/dev/level-editor/AGENT_API.md).
//
//   petari-level help [command|--op NAME]
//   petari-level [--ws DIR] init --disc DISC --galaxy EggStarGalaxy [--mission 1] [--user DIR] [--user-mods]
//   petari-level [--ws DIR] <command> [{json args}] [--key value ...] [positional]
//   petari-level [--ws DIR] apply ops.json|-          (a JSON array of ops, or {"ops": [...], "key": ...})
//   petari-level [--ws DIR] serve                     JSON lines on stdin: {"id": 1, "cmd": "list", "args": {...}}
//
// A workspace (default ./level-ws, or $PETARI_LEVEL_WS) is level.json: the disc, galaxy, mission and
// user directory it was opened with, and the journal. Each call opens the galaxy, replays the journal,
// runs the command, and saves the journal after a mutating command. Output: one JSON object on stdout
// ({"ok": true, ...} or {"ok": false, "error": {...}}); exit 0, 1 (error) or 2 (usage).
#include <petari/editor/level_api.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace PetariEditor;
using PetariEditor::Json::Value;

namespace {
    bool gPretty = false;

    void emit(const Value& v) {
        std::fputs(Json::write(v, gPretty).c_str(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

    int emitError(const std::string& code, const std::string& message, const std::string& hint = "", int exitCode = 1) {
        LevelApi::Error e{code, message, hint, {}, ""};
        Value out = Value::object();
        out.set("ok", false);
        out.set("error", e.toJson());
        emit(out);
        return exitCode;
    }

    std::optional< std::string > readText(const std::string& path) {
        if (path == "-") return std::string(std::istreambuf_iterator< char >(std::cin), {});
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        return std::string(std::istreambuf_iterator< char >(in), {});
    }

    bool writeText(const fs::path& path, const std::string& text) {
        const fs::path tmp = path.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out) return false;
        }
        std::error_code ec;
        fs::rename(tmp, path, ec);
        return !ec;
    }

    // "--key value": JSON when it parses ("3", "true", "[1,2,3]", "{...}"), "x,y,z" as a vector, else a string.
    Value flagValue(const std::string& text) {
        std::string error;
        if (std::optional< Value > v = Json::parse(text, &error)) return *v;
        float x, y, z;
        char tail;
        if (std::sscanf(text.c_str(), "%f,%f,%f%c", &x, &y, &z, &tail) == 3) {
            Value a = Value::array();
            a.push(static_cast< double >(x));
            a.push(static_cast< double >(y));
            a.push(static_cast< double >(z));
            return a;
        }
        return Value(text);
    }

    // The first positional's key per command.
    const char* positionalKey(const std::string& command) {
        if (command == "class") return "class";
        if (command == "get") return "ref";
        if (command == "catalog") return "query";
        if (command == "save") return "mod";
        if (command == "help") return "command";
        if (command == "list") return "class";
        return nullptr;
    }

    struct Workspace {
        fs::path dir;
        Value config = Value::object();
        fs::path file() const { return dir / "level.json"; }
    };

    std::unique_ptr< LevelApi::Level > openWorkspace(Workspace& ws, int* pExit) {
        const std::optional< std::string > text = readText(ws.file().string());
        if (!text) {
            *pExit = emitError("no-workspace", "no workspace at " + ws.dir.string(),
                               "create one: petari-level --ws " + ws.dir.string() + " init --disc <disc> --galaxy <Galaxy>", 2);
            return nullptr;
        }
        std::string error;
        std::optional< Value > config = Json::parse(*text, &error);
        if (!config) {
            *pExit = emitError("bad-workspace", ws.file().string() + ": " + error);
            return nullptr;
        }
        ws.config = *config;
        LevelApi::OpenOptions options;
        options.disc = ws.config["disc"].asString();
        options.user = ws.config["user"].asString();
        options.userMods = ws.config["user_mods"].asBool();
        options.galaxy = ws.config["galaxy"].asString();
        options.mission = ws.config["mission"].asInt(1);
        LevelApi::Error e;
        std::unique_ptr< LevelApi::Level > level = LevelApi::Level::open(options, &e);
        if (!level) {
            Value out = Value::object();
            out.set("ok", false);
            out.set("error", e.toJson());
            emit(out);
            *pExit = 1;
            return nullptr;
        }
        if (!level->replay(ws.config["journal"], &e)) {
            Value out = Value::object();
            out.set("ok", false);
            out.set("error", e.toJson());
            emit(out);
            *pExit = 1;
            return nullptr;
        }
        return level;
    }

    bool saveWorkspace(Workspace& ws, const LevelApi::Level& level) {
        ws.config.set("journal", level.journal());
        return writeText(ws.file(), Json::write(ws.config, true) + "\n");
    }

    bool mutates(const std::string& command) {
        for (const LevelApi::CommandSpec& s : LevelApi::commands()) {
            if (s.name == command) return s.mutates;
        }
        return false;
    }

    int usage() {
        std::fputs(
            "petari-level: author Super Mario Galaxy levels with the Petari editor's core, in JSON.\n"
            "  petari-level help [command] [--op NAME] [--all]      self-description (JSON)\n"
            "  petari-level [--ws DIR] init --disc DISC --galaxy NAME [--mission N] [--user DIR] [--user-mods]\n"
            "  petari-level [--ws DIR] <command> [{json}] [--key value ...]\n"
            "  petari-level [--ws DIR] apply FILE|-                 a batch: [op, ...] or {\"ops\": [...], \"key\": ...}\n"
            "  petari-level [--ws DIR] serve                        JSON lines: {\"id\":1,\"cmd\":\"list\",\"args\":{}}\n"
            "Global: --ws DIR (default $PETARI_LEVEL_WS or ./level-ws), --pretty, --time\n",
            stderr);
        return 2;
    }
}  // namespace

int main(int argc, char** argv) {
    std::vector< std::string > words(argv + 1, argv + argc);
    Workspace ws;
    ws.dir = std::getenv("PETARI_LEVEL_WS") != nullptr ? std::getenv("PETARI_LEVEL_WS") : "level-ws";
    bool timing = false;
    // Global flags first.
    std::vector< std::string > rest;
    for (std::size_t i = 0; i < words.size(); i++) {
        if (words[i] == "--ws" && i + 1 < words.size()) {
            ws.dir = words[++i];
        } else if (words[i] == "--pretty") {
            gPretty = true;
        } else if (words[i] == "--time") {
            timing = true;
        } else {
            rest.push_back(words[i]);
        }
    }
    if (rest.empty() || rest[0] == "--help" || rest[0] == "-h") return usage();
    const std::string command = rest[0];

    // Args: {json} objects, --key value, a positional, and for apply a file.
    Value args = Value::object();
    for (std::size_t i = 1; i < rest.size(); i++) {
        const std::string& w = rest[i];
        if (!w.empty() && (w[0] == '{' || w[0] == '[')) {
            std::string error;
            std::optional< Value > v = Json::parse(w, &error);
            if (!v) return emitError("bad-args", "argument " + std::to_string(i) + " is not JSON: " + error, "", 2);
            if (v->isArray()) {
                args.set("ops", *v);
            } else {
                for (const auto& [k, value] : v->members()) args.set(k, value);
            }
        } else if (w.rfind("--", 0) == 0) {
            std::string key = w.substr(2);
            for (char& c : key) {
                if (c == '-') c = '_';
            }
            if (i + 1 < rest.size() && rest[i + 1].rfind("--", 0) != 0) {
                args.set(key, flagValue(rest[++i]));
            } else {
                args.set(key, true);
            }
        } else if (command == "apply" && !args.has("ops")) {
            const std::optional< std::string > text = readText(w);
            if (!text) return emitError("bad-args", "cannot read " + w, "", 2);
            std::string error;
            std::optional< Value > v = Json::parse(*text, &error);
            if (!v) return emitError("bad-args", w + " is not JSON: " + error, "", 2);
            if (v->isArray()) {
                args.set("ops", *v);
            } else {
                for (const auto& [k, value] : v->members()) args.set(k, value);
            }
        } else if (const char* key = positionalKey(command); key != nullptr && !args.has(key)) {
            args.set(key, w);
        } else {
            return emitError("bad-args", "unexpected argument '" + w + "'", "use --key value or a JSON object", 2);
        }
    }

    if (command == "help") {
        // Static: no disc or workspace needed. ("help add" names an op when no command has that name.)
        if (args.has("command")) {
            bool isCommand = false;
            for (const LevelApi::CommandSpec& s : LevelApi::commands()) isCommand = isCommand || s.name == args["command"].asString();
            if (!isCommand) {
                args.set("op", args["command"]);
                Value copy = Value::object();
                for (const auto& [k, v] : args.members()) {
                    if (k != "command") copy.set(k, v);
                }
                args = copy;
            }
        }
        Value out = LevelApi::Level::help(args);
        if (!args.has("command") && !args.has("op")) {
            out.set("cli", "petari-level [--ws DIR] <command> [{json}] [--key value]; init/serve are CLI-only (see --help)");
        }
        emit(out);
        return out["ok"].asBool() ? 0 : 1;
    }

    if (command == "init") {
        std::error_code ec;
        if (fs::exists(ws.file(), ec) && !args["force"].asBool()) {
            return emitError("workspace-exists", ws.file().string() + " exists", "use another --ws, or --force to start over (the journal is lost)");
        }
        fs::create_directories(ws.dir, ec);
        Value config = Value::object();
        const fs::path disc = fs::absolute(args["disc"].asString(), ec);
        config.set("disc", disc.string());
        config.set("galaxy", args["galaxy"].asString());
        config.set("mission", args["mission"].asInt(1));
        const std::string user = args.has("user") ? args["user"].asString() : (ws.dir / "user").string();
        config.set("user", fs::absolute(user, ec).string());
        config.set("user_mods", args["user_mods"].asBool());
        ws.config = config;
        if (!writeText(ws.file(), Json::write(config, true) + "\n")) return emitError("io", "cannot write " + ws.file().string());
        int exitCode = 0;
        std::unique_ptr< LevelApi::Level > level = openWorkspace(ws, &exitCode);
        if (!level) {
            fs::remove(ws.file(), ec);
            return exitCode;
        }
        saveWorkspace(ws, *level);
        Value out = level->call("info", Value::object());
        out.set("workspace", fs::absolute(ws.dir, ec).string());
        emit(out);
        return 0;
    }

    const auto t0 = std::chrono::steady_clock::now();
    int exitCode = 0;
    std::unique_ptr< LevelApi::Level > level = openWorkspace(ws, &exitCode);
    if (!level) return exitCode;
    const double openMs = std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - t0).count();

    if (command == "serve") {
        // One JSON request per line; one JSON reply per line (with the request's id).
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            std::string error;
            std::optional< Value > request = Json::parse(line, &error);
            Value reply;
            if (!request || !request->isObject()) {
                reply = Value::object();
                reply.set("ok", false);
                reply.set("error", LevelApi::Error{"bad-request", "not a JSON object: " + error, "{\"id\": 1, \"cmd\": \"info\", \"args\": {}}", {}, ""}.toJson());
            } else {
                const std::string cmd = (*request)["cmd"].asString();
                if (cmd == "quit") break;
                reply = level->call(cmd, (*request)["args"]);
                if (reply["ok"].asBool() && mutates(cmd) && !(*request)["args"]["dry_run"].asBool()) saveWorkspace(ws, *level);
            }
            if (request && request->has("id")) reply.set("id", (*request)["id"]);
            emit(reply);
        }
        return 0;
    }

    Value out = level->call(command, args);
    if (out["ok"].asBool() && mutates(command) && !args["dry_run"].asBool()) {
        if (!saveWorkspace(ws, *level)) return emitError("io", "cannot write " + ws.file().string());
    }
    if (timing) {
        Value t = Value::object();
        t.set("open_ms", std::round(openMs));
        t.set("total_ms", std::round(std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - t0).count()));
        out.set("timing", t);
    }
    emit(out);
    return out["ok"].asBool() ? 0 : 1;
}
