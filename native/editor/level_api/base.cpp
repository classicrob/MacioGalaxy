#include "base.hpp"

#include "model_io.hpp"
#include <petari/editor/patch.hpp>
#include <petari/platform/dvd.hpp>
#include <petari/platform/mod_folder.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unistd.h>

namespace PetariEditor::LevelApi {
    namespace fs = std::filesystem;
    namespace Mods = PetariNative::Platform::ModFolder;

    namespace {
        struct Hash {
            std::uint64_t value = 14695981039346656037ull;
            void bytes(const void* data, std::size_t size) {
                const auto* p = static_cast< const unsigned char* >(data);
                for (std::size_t i = 0; i < size; ++i)
                    value = (value ^ p[i]) * 1099511628211ull;
            }
            void text(const std::string& s) {
                bytes(s.data(), s.size());
                const char delimiter = 0;
                bytes(&delimiter, 1);
            }
            std::string str() const {
                std::ostringstream out;
                out << std::hex << std::setfill('0') << std::setw(16) << value;
                return out.str();
            }
        };

        fs::path identityPath(const fs::path& path) {
            std::error_code ec;
            fs::path result = fs::weakly_canonical(path, ec);
            return ec ? fs::absolute(path).lexically_normal() : result;
        }

        std::string contentHash(const fs::path& root) {
            std::error_code ec;
            if (!fs::is_directory(root, ec))
                return "missing";
            std::vector< fs::path > files;
            for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
                if (it->is_regular_file(ec))
                    files.push_back(it->path());
            }
            if (ec)
                fail("io", "cannot read mod directory: " + root.string() + ": " + ec.message());
            std::sort(files.begin(), files.end());
            Hash hash;
            char buffer[65536];
            for (const fs::path& path : files) {
                hash.text(path.lexically_relative(root).generic_string());
                std::ifstream in(path, std::ios::binary);
                if (!in)
                    fail("io", "cannot read " + path.string());
                while (in) {
                    in.read(buffer, sizeof(buffer));
                    hash.bytes(buffer, static_cast< std::size_t >(in.gcount()));
                }
                if (!in.eof())
                    fail("io", "cannot finish reading " + path.string());
                hash.text("");
            }
            return hash.str();
        }

        Json::Value sourceIdentity(const Mods::ModInfo& mod) {
            Json::Value result = Json::Value::object();
            result.set("name", mod.name);
            result.set("path", identityPath(mod.root).string());
            result.set("hash", contentHash(mod.root));
            result.set("accepted_hash", result["hash"]);
            return result;
        }

        std::optional< Formats::Bytes > readFile(const fs::path& file) {
            std::ifstream in(file, std::ios::binary);
            if (!in)
                return std::nullopt;
            return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
        }
    }  // namespace

    Json::Value freezeBase(const OpenOptions& options) {
        Json::Value base = Json::Value::object();
        base.set("format", 1);
        Json::Value sources = Json::Value::array();
        Json::Value files = Json::Value::array();
        std::vector< Mods::ModInfo > mods;
        std::map< std::string, bool > states;
        if (options.userMods) {
            mods = Mods::discover(Mods::searchDirectories(options.user));
            states = Mods::effectiveStates(options.user / "mods.txt");
        }
        if (!options.mod.empty()) {
            const fs::path root = identityPath(options.mod);
            const auto discovered = Mods::discover({root.parent_path()});
            auto found = std::find_if(discovered.begin(), discovered.end(), [&](const auto& mod) { return identityPath(mod.root) == root; });
            if (found == discovered.end() || !found->hasEdits || !fs::exists(root / "edits" / (options.galaxy + ".json")))
                fail("bad-mod", "no saved patch for " + options.galaxy + " at " + root.string(), "pass a mod folder with edits/<Galaxy>.json", {},
                     "mod");
            mods.erase(std::remove_if(mods.begin(), mods.end(), [&](const auto& m) { return m.name == found->name; }), mods.end());
            Mods::ModInfo explicitMod = *found;
            explicitMod.priority = std::numeric_limits< int >::max();
            states[explicitMod.name] = true;
            mods.push_back(std::move(explicitMod));
        }
        std::sort(mods.begin(), mods.end(),
                  [](const auto& a, const auto& b) { return a.priority != b.priority ? a.priority > b.priority : a.name < b.name; });
        for (const auto& mod : mods) {
            if (!states[mod.name])
                continue;
            const fs::path cache = identityPath(options.baseDirectory);
            const fs::path root = identityPath(mod.root);
            const std::string relative = cache.lexically_relative(root).generic_string();
            if (relative.empty() || (relative != ".." && relative.rfind("../", 0) != 0))
                fail("bad-workspace", "the workspace base is inside its input mod", "put the workspace outside the mod folder", {}, "base");
            sources.push(sourceIdentity(mod));
        }
        base.set("sources", sources);
        base.set("outputs", Json::Value::array());
        if (sources.items().empty()) {
            base.set("files", files);
            return base;
        }
        const fs::path target = options.baseDirectory;
        const fs::path pending = target.string() + ".pending-" + std::to_string(getpid()) + "-" +
                                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::error_code ec;
        fs::create_directories(pending, ec);
        if (ec)
            fail("io", "cannot create frozen base: " + ec.message());
        try {
            // The canonical patch builder without asset builders preserves editable EditorBrush rows.
            Mods::setPatchBuilder(&Patch::buildForLoader);
            Mods::PatchContext context{PetariNative::Platform::DVD::mountInfo().filesDirectory, pending / "generated"};
            const Mods::Resolved resolved = Mods::resolve(mods, states, &context);
            for (const std::string& line : resolved.log) {
                if (line.find("skipped") != std::string::npos || line.find("left without") != std::string::npos ||
                    line.find("not applied") != std::string::npos || line.find("not a valid") != std::string::npos ||
                    line.find("cannot write") != std::string::npos)
                    fail("bad-mod", "the mod could not be frozen: " + line, "repair the patch or use its matching disc", {}, "mod");
            }
            for (const auto& file : resolved.files) {
                const std::string path = Mods::normalizePath(file.path);
                if (path.empty())
                    fail("bad-mod", "invalid overlay path: " + file.path);
                const fs::path destination = pending / "files" / path;
                fs::create_directories(destination.parent_path());
                fs::copy_file(file.host, destination, fs::copy_options::overwrite_existing);
                files.push(path);
            }
            // A source changing during capture cannot produce a trustworthy continuation base.
            for (const auto& source : sources.items()) {
                if (contentHash(source["path"].asString()) != source["hash"].asString())
                    fail("stale-source", "input mod changed while freezing it", "retry init after the writer finishes", {}, "mod");
            }
            fs::remove_all(pending / "generated");
            const fs::path previous = target.string() + ".previous-" + std::to_string(getpid());
            fs::remove_all(previous, ec);
            const bool existed = fs::exists(target);
            if (existed)
                fs::rename(target, previous);
            try {
                fs::rename(pending, target);
            } catch (...) {
                if (existed)
                    fs::rename(previous, target);
                throw;
            }
            fs::remove_all(previous, ec);
        } catch (...) {
            fs::remove_all(pending, ec);
            throw;
        }
        base.set("files", files);
        return base;
    }

    FileSource frozenSource(const OpenOptions& options, const Json::Value& base) {
        auto files = std::make_shared< std::map< std::string, fs::path > >();
        for (const auto& item : base["files"].items()) {
            const std::string path = Mods::normalizePath(item.asString());
            if (path.empty())
                fail("bad-workspace", "invalid frozen file path", "re-freeze the workspace", {}, "base.files");
            const fs::path host = options.baseDirectory / "files" / path;
            if (!fs::is_regular_file(host))
                fail("bad-workspace", "frozen file is missing: " + host.string(), "restore the workspace base or re-freeze", {}, "base.files");
            files->emplace(lower("/" + path), host);
        }
        return [files](const std::string& path) -> std::optional< Formats::Bytes > {
            const auto found = files->find(lower("/" + Mods::normalizePath(path)));
            return found == files->end() ? ModelIO::readUnmodded(path) : readFile(found->second);
        };
    }

    Json::Value sourceWarnings(const OpenOptions& options, const Json::Value& base) {
        Json::Value warnings = Json::Value::array();
        std::set< std::string > known;
        for (const auto& source : base["sources"].items()) {
            const std::string path = source["path"].asString();
            known.insert(identityPath(path).string());
            std::string current;
            try {
                current = contentHash(path);
            } catch (const Failure&) {
                current = "unreadable";
            } catch (const std::exception&) {
                current = "unreadable";
            }
            const std::string expected = source["accepted_hash"].asString(source["hash"].asString());
            if (current == expected)
                continue;
            Json::Value warning = Json::Value::object();
            warning.set("code", "stale-source");
            warning.set("name", source["name"]);
            warning.set("path", path);
            warning.set("expected_hash", expected);
            warning.set("current_hash", current);
            warning.set("message", "The input changed; this workspace still uses its frozen base.");
            warning.set("hint", "Save your work, then refreeze with discard_journal:true to adopt the current input.");
            warnings.push(std::move(warning));
        }
        if (options.userMods) {
            for (const auto& output : base["outputs"].items())
                known.insert(identityPath(output.asString()).string());
            std::set< std::string > enabled;
            for (const fs::path& root : Mods::enabledRoots(options.user))
                enabled.insert(identityPath(root).string());
            if (!options.mod.empty())
                enabled.insert(identityPath(options.mod).string());
            bool changed = false;
            for (const auto& path : enabled)
                if (!known.count(path))
                    changed = true;
            for (const auto& source : base["sources"].items())
                if (!enabled.count(identityPath(source["path"].asString()).string()))
                    changed = true;
            if (changed) {
                Json::Value warning = Json::Value::object();
                warning.set("code", "stale-source");
                warning.set("message", "The enabled mod list changed; this workspace still uses its frozen base.");
                warning.set("hint", "Save your work, then explicitly refreeze.");
                warnings.push(std::move(warning));
            }
        }
        return warnings;
    }

    void recordSavedSource(Json::Value& base, const fs::path& root) {
        const std::string path = identityPath(root).string();
        Json::Value sources = Json::Value::array();
        bool matched = false;
        for (auto source : base["sources"].items()) {
            if (identityPath(source["path"].asString()).string() == path) {
                source.set("accepted_hash", contentHash(root));
                matched = true;
            }
            sources.push(std::move(source));
        }
        base.set("sources", sources);
        if (!matched) {
            Json::Value outputs = base["outputs"];
            bool exists = false;
            for (const auto& output : outputs.items())
                exists |= output.asString() == path;
            if (!exists)
                outputs.push(path);
            base.set("outputs", outputs);
        }
    }

    std::string modelFingerprint(const ModelView& model) {
        Hash hash;
        hash.text(model.galaxy());
        hash.text(std::to_string(model.scenario()));
        for (int z = 0; z < model.zoneCount(); ++z) {
            hash.text(model.zoneName(z));
            const Mtx34 matrix = model.zoneWorldMatrix(z);
            for (const auto& row : matrix.m)
                for (float value : row)
                    hash.bytes(&value, sizeof(value));
            for (int t = 0; t < model.tableCount(z); ++t) {
                hash.text(model.tablePath(z, t));
                for (const Column& column : model.columns(z, t)) {
                    hash.text(std::to_string(column.hash));
                    hash.text(std::to_string(static_cast< int >(column.type)));
                }
                model.forEachRow(z, t, [&](RowId row) {
                    if (model.state(row) == RowState::Deleted)
                        return;
                    hash.text("row");
                    for (const Column& column : model.columns(z, t)) {
                        const Cell* cell = model.cell(row, column.hash);
                        hash.text(cell ? std::to_string(static_cast< int >(cell->type)) : "null");
                        if (!cell)
                            continue;
                        if (cell->type == CellType::String || cell->type == CellType::StringOffset)
                            hash.text(cell->s);
                        else if (cell->type == CellType::Float)
                            hash.bytes(&cell->f, sizeof(cell->f));
                        else
                            hash.text(std::to_string(cell->i));
                    }
                });
                hash.text("end-table");
            }
        }
        return hash.str();
    }

    std::string normalizedCheckKey(const EditModel& model, const Check& check) {
        std::map< std::string, std::string > replacements;
        for (RowId row : check.rows) {
            int z = -1, t = -1;
            if (!model.locate(row, &z, &t))
                continue;
            const int disc = model.discRowIndex(row);
            const std::string identity = disc < 0 ?
                                             "mod/" + rowRef(model, row) :
                                             "disc/" + std::string(model.zoneName(z)) + "/" + model.tablePath(z, t) + "/" + std::to_string(disc);
            replacements[std::to_string(row.v)] = identity;
            std::ostringstream hex;
            hex << std::hex << row.v;
            // Brush check keys use hex, other providers decimal.
            if (check.key.rfind("brush-tris:", 0) == 0)
                replacements[hex.str()] = identity;
        }
        std::string out;
        std::istringstream tokens(check.key);
        std::string token;
        while (std::getline(tokens, token, ':')) {
            if (!out.empty())
                out += ':';
            auto found = replacements.find(token);
            out += found == replacements.end() ? token : found->second;
        }
        return out;
    }

    bool Level::Impl::isBaseline(const Check& check) const {
        return baseline.count(normalizedCheckKey(*model, check)) != 0;
    }
}  // namespace PetariEditor::LevelApi
