// Tests for the agent level-authoring API (native/editor/level_api, petari-level) on Good Egg from the
// extracted disc: queries, batches as one undo step, atomicity, idempotent keys, named args, refs,
// errors with candidates, the journal's replay, checks and their fixes, and a patch save loaded back.
// Skipped without the disc.
#include <petari/editor/level_api.hpp>
#include <petari/editor/paths.hpp>

#include "edit_model.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
namespace L = PetariEditor::LevelApi;
using PetariEditor::Json::Value;

namespace {
int sChecks = 0;
void check(bool condition, const std::string& label) {
    ++sChecks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
        std::exit(1);
    }
}

Value parse(const char* text) {
    std::string error;
    std::optional< Value > v = PetariEditor::Json::parse(text, &error);
    if (!v) {
        std::fprintf(stderr, "bad test JSON: %s\n%s\n", error.c_str(), text);
        std::exit(1);
    }
    return *v;
}

Value call(L::Level& level, const char* command, const char* args = "{}") {
    return level.call(command, parse(args));
}

std::string show(const Value& v) {
    return PetariEditor::Json::write(v);
}

bool ok(const Value& reply, const std::string& what) {
        if (!reply["ok"].asBool())
            std::fprintf(stderr, "%s: %s\n", what.c_str(), show(reply).c_str());
    return reply["ok"].asBool();
}

bool hasCandidate(const Value& reply, const std::string& text) {
    for (const Value& c : reply["error"]["candidates"].items()) {
            if (c.asString().find(text) != std::string::npos)
                return true;
    }
    return false;
}

double ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - since).count();
}
    void testModContinuation(const L::OpenOptions& originalOptions) {
        auto options = originalOptions;
        options.user /= "mod_continuation";
        options.baseDirectory = options.user / "workspace" / "base";
        L::Error error;
        auto stock = L::Level::open(options, &error);
        check(stock != nullptr, "stock for continuation: " + error.message);
        const int originalCount = call(*stock, "info")["objects"].asInt();
        const Value stockCoin = call(*stock, "get", R"({"ref":"Coin#0"})");
        const auto originalRow = L::parseRowRef(stock->model(), stockCoin["ref"].asString());
        const int originalIndex = stock->model().discRowIndex(originalRow);
        check(originalIndex >= 0, "original row has a disc index");
        const Value nextCoin = call(*stock, "get", R"({"ref":"Coin#1"})");
        const auto survivingRow = L::parseRowRef(stock->model(), nextCoin["ref"].asString());
        const int survivingIndex = stock->model().discRowIndex(survivingRow);
        check(ok(call(*stock, "apply", R"({"ops":[{"op":"delete","ref":"Coin#0"}]})"), "delete original"), "delete original row for mapping test");
        check(stock->model().discRowIndex(survivingRow) == survivingIndex, "surviving row keeps its disc index after another deletion");
        check(ok(call(*stock, "undo"), "restore original"), "restore original row");
        check(stock->model().discRowIndex(originalRow) == originalIndex, "undo restores the original disc index");
        const auto savedOps = call(*stock, "apply", R"({"ops":[{"op":"add","class":"Coin","zone":"DiskGardenZone","pos":[0,70000,0]},{"op":"add","class":"SuperSpinDriver","zone":"DiskGardenZone","pos":[0,71000,0]}]})");
        check(ok(savedOps, "source patch objects"), "source patch created");
        const auto addedRow = L::parseRowRef(stock->model(), savedOps["created"].items()[0]["ref"].asString());
        check(stock->model().discRowIndex(addedRow) == -1, "session addition has no disc index");
        check(ok(call(*stock, "save", R"({"mod":"Continue me"})"), "source save"), "source patch saved");
        stock.reset();
        options.mod = options.user / "mods" / "Continue me";
        auto modded = L::Level::open(options, &error);
        check(modded != nullptr, "open explicit mod: " + error.message);
        Value info = call(*modded, "info");
        check(info["objects"].asInt() == originalCount + 2, "explicit mod is the base");
        check(info["baseline"].asString() == "pristine-disc", "continued mod baseline is pristine disc");
        check(info["missions"].items()[0]["name"].asString() == "Dino Piranha", "continued mod info exposes the English mission name");
        check(info["source_warnings"].items().empty(), "unchanged input has no warning");
        check(!call(*modded, "checks")["checks"].items().empty(), "mod's own missing-path check stays visible");
        check(modded->base()["sources"].items().size() == 1 && !modded->base()["files"].items().empty(), "source identity and cached overlays recorded");
        for (const auto& file : modded->base()["files"].items()) check(fs::exists(options.baseDirectory / "files" / file.asString()), "frozen overlay lives inside workspace");
        check(ok(call(*modded, "apply", R"({"ops":[{"op":"add","class":"Coin","zone":"DiskGardenZone","pos":[0,72000,0],"as":"extra"}]})"), "continue edit"), "continue editing");
        const Value journal = modded->journal();
        check(ok(call(*modded, "save", R"({"mod":"Continue me"})"), "save over source"), "save over input mod");
        check(call(*modded, "info")["source_warnings"].items().empty(), "own save does not stale the source");
        options.base = modded->base();
        modded.reset();
        auto reopened = L::Level::open(options, &error);
        check(reopened && reopened->replay(journal, &error), "reopen frozen base and journal: " + error.message);
        check(call(*reopened, "info")["objects"].asInt() == originalCount + 3, "saved journal not double applied");
        check(call(*reopened, "info")["source_warnings"].items().empty(), "accepted written hash persists across reopen");
        {
            std::ofstream manifest(options.mod / "mod.txt", std::ios::app);
            manifest << "description=changed externally\n";
        }
        const Value stale = call(*reopened, "info");
        check(stale["source_warnings"].items().size() == 1 && stale["objects"].asInt() == originalCount + 3, "stale input warns without rebasing");
        check(call(*reopened, "refreeze")["error"]["code"].asString() == "journal", "refreeze refuses implicit journal loss");
        check(ok(call(*reopened, "refreeze", R"({"discard_journal":true})"), "refreeze"), "explicit refreeze adopts current source");
        check(call(*reopened, "info")["history"].asInt() == 0 && call(*reopened, "info")["objects"].asInt() == originalCount + 3, "refreeze clears journal without duplicating saved edits");
        check(call(*reopened, "info")["source_warnings"].items().empty(), "refreeze clears stale warning");
        const Value frozen = reopened->base();
        const fs::path copied = options.user / "copied-workspace" / "base";
        fs::create_directories(copied.parent_path());
        fs::copy(options.baseDirectory, copied, fs::copy_options::recursive);
        options.baseDirectory = copied;
        options.base = frozen;
        auto copy = L::Level::open(options, &error);
        check(copy && call(*copy, "info")["objects"].asInt() == originalCount + 3, "copying workspace carries all frozen files");
        auto enabled = options;
        enabled.mod.clear();
        enabled.userMods = true;
        enabled.base = Value();
        enabled.baseDirectory = options.user / "enabled-workspace" / "base";
        auto userMods = L::Level::open(enabled, &error);
        check(userMods && !call(*userMods, "checks")["checks"].items().empty(), "user-mods also exposes input mod problems");
        check(call(*userMods, "info")["baseline"].asString() == "pristine-disc", "user-mods baseline is pristine disc");
        enabled.mod = options.user / "missing";
        check(!L::Level::open(enabled, &error) && error.code == "bad-mod", "missing mod is a structured error");
    }

    void testGroundArrays(const L::OpenOptions& originalOptions) {
        auto options = originalOptions;
        options.user /= "ground_arrays";
        L::Error error;
        auto level = L::Level::open(options, &error);
        check(level != nullptr, "ground array test open: " + error.message);
        auto& lv = *level;
        check(ok(call(lv, "apply", R"({"ops":[{"op":"add","class":"EditorBrush","pos":[0,50000,0],"scale":[0.6,0.1,0.6],"args":{"Shape":"Box","Gravity":0},"as":"ground"}]})"), "ground brush"), "controlled ground for array test");
        Value placed = call(lv, "apply", R"({"ops":[{"op":"ground_array","class":"EditorBrush","shape":"ring","center":[0,50500,0],"radius":50,"count":3,"height":50,"down":[0,-1,0],"front":[1,0,1],"scale":[0.15,0.1,0.15],"args":{"Shape":"Box","Gravity":0},"as":"copies"}]})");
        check(ok(placed, "ground array") && placed["placements"].items().size() == 3, "ground array reports each preflight hit");
        for (int i = 0; i < 3; ++i) {
            Value args = Value::object(); args.set("ref", "@copies." + std::to_string(i));
            const Value copy = lv.call("get", args);
            check(std::fabs(copy["pos"].items()[1].asNumber() - 50150) < .1, "overlapping copies all use original ground, not earlier copies");
            check(std::fabs(copy["rot"].items()[1].asNumber() - 45) < .1, "front retains projected heading while standing against gravity");
            check(placed["placements"].items()[i]["on"].asString() == call(lv, "get", R"({"ref":"@ground"})")["ref"].asString(), "placement evidence names original collider");
        }
        const auto before = lv.journal();
        Value failed = call(lv, "apply", R"({"ops":[{"op":"ground_array","class":"Coin","shape":"line","start":[0,50500,0],"end":[5000,50500,0],"count":2,"down":[0,-1,0],"as":"missing"}]})");
        check(failed["error"]["code"].asString() == "no-ground" && show(lv.journal()) == show(before), "one missing probe refuses the entire ground array");
        check(call(lv, "apply", R"({"ops":[{"op":"ground_array","class":"Coin","shape":"ring","center":[0,50500,0],"radius":50,"count":2,"down":[0,0,0]}]})")["error"]["code"].asString() == "bad-value", "zero gravity ray direction is rejected");
    }

    void testGravityEdits(const L::OpenOptions& originalOptions) {
        auto options = originalOptions;
        options.user /= "gravity";
        L::Error error;
        auto level = L::Level::open(options, &error);
        check(level != nullptr, "gravity test open: " + error.message);
        auto& lv = *level;
        check(call(lv, "class", R"({"class":"EditorBrush"})")["args"].items().size() == 4,
              "shared loader registers the brush catalog with four named args");
        check(ok(call(lv, "apply", R"({"ops":[{"op":"add","class":"GlobalPointGravity","pos":[0,3000,0],"as":"g"},{"op":"gravity-shape","ref":"@g","shape":"Parallel","bound":"Box"},{"op":"gravity-values","ref":"@g","values":{"Range":-20,"Distant":-5,"Priority":3,"Inverse":true}},{"op":"add","class":"EditorBrush","pos":[0,4000,0],"scale":[0.3,0.1,0.3],"args":{"Shape":"Box","Gravity":"Down only"},"as":"brush"}]})"), "gravity and brush batch"),
              "canonical gravity operations and named brush enums share one transaction");
        const Value gravity = call(lv, "get", R"({"ref":"@g"})");
        check(gravity["class"].asString() == "GlobalPlaneGravityInBox", "shape helper chooses canonical bounded parallel class");
        const auto gravityRow = L::parseRowRef(lv.model(), gravity["ref"].asString());
        check(lv.model().cell(gravityRow, PetariEditor::columnHash("Range"))->f == -1 &&
              lv.model().cell(gravityRow, PetariEditor::columnHash("Distant"))->f == 0 &&
              lv.model().cell(gravityRow, PetariEditor::columnHash("Priority"))->i == 3 &&
              lv.model().cell(gravityRow, PetariEditor::columnHash("Inverse"))->i == 1, "numeric helper preserves canonical clamp and cell semantics");
        const auto before = lv.journal();
        check(call(lv, "apply", R"({"ops":[{"op":"gravity-values","ref":"@g","values":{"Priority":2}},{"op":"gravity-shape","ref":"@g","shape":"Wire"}]})")["error"]["code"].asString() == "op-failed" &&
              show(lv.journal()) == show(before), "wire without a rail rejects and rolls back earlier gravity changes");
        check(call(lv, "apply", R"({"ops":[{"op":"gravity-values","ref":"@brush","values":{"Range":100}}]})")["error"]["code"].asString() == "bad-ref", "non-gravity target is rejected");
        check(call(lv, "apply", R"({"ops":[{"op":"gravity-values","ref":"@g","values":{"Priorty":2}}]})")["error"]["code"].asString() == "bad-arg", "unknown gravity value is rejected with candidates");
        check(ok(call(lv, "apply", R"({"ops":[{"op":"path","points":[[0,3000,0],[1000,3000,0]],"as":"wire"},{"op":"link","ref":"@g","path":"@wire"},{"op":"gravity-shape","ref":"@g","shape":"Wire"}]})"), "wire link"),
              "wire conversion uses a canonical linked path");
        auto copy = L::Level::open(options, &error);
        check(copy && copy->replay(lv.journal(), &error), "gravity/brush journal replays");
        check(show(call(*copy, "get", R"({"ref":"@g"})")) == show(call(lv, "get", R"({"ref":"@g"})")), "gravity value types survive replay");
        check(show(call(*copy, "get", R"({"ref":"@brush"})")) == show(call(lv, "get", R"({"ref":"@brush"})")), "brush remains an editable catalog row");
    }

    void testPathEdits(const L::OpenOptions& originalOptions) {
        auto options = originalOptions;
        options.user /= "paths";
        L::Error error;
        auto level = L::Level::open(options, &error);
        check(level != nullptr, "path test open: " + error.message);
        auto& lv = *level;
        check(
            ok(call(
                   lv, "apply",
                   R"({"ops":[{"op":"path","zone":"DiskGardenZone","points":[[0,400,950],[0,700,950],[0,1000,950]],"smooth":false,"as":"editable"}]})"),
               "path create"),
            "create path in rotated zone");
        const Value original = call(lv, "get", R"({"ref":"@editable"})");
        Value edits = Value::object(), ops = Value::array(), label = Value::object();
        label.set("op", "label");
        label.set("ref", original["points"].items()[0]["ref"]);
        label.set("as", "first");
        ops.push(label);
        ops.push(parse(R"({"op":"insert-point","path":"@editable","segment":0,"as":"mid"})"));
        ops.push(parse(R"({"op":"delete-point","ref":"@first"})"));
        edits.set("ops", ops);
        edits.set("show_op", true);
        const int cursor = lv.model().historyCursor();
        Value inserted = lv.call("apply", edits);
        check(ok(inserted, "insert/delete") && lv.model().historyCursor() == cursor + 1, "mixed insertion/deletion is one undo transaction");
        Value rail = call(lv, "get", R"({"ref":"@editable"})");
        check(rail["points"].items().size() == 3 &&
                  rail["points"].items()[0]["ref"].asString() == call(lv, "get", R"({"ref":"@mid"})")["ref"].asString(),
              "merged insertion remains before the surviving original point");
        check(inserted["op"]["adds"].items()[0]["at"].asInt(-1) == 1, "ordered add accounts for rows deleted later by core");
        check(std::fabs(rail["points"].items()[0]["pos"].items()[1].asNumber() - 550) < 0.1, "de Casteljau midpoint survives world/zone conversion");
        const Value midpoint = call(lv, "get", R"({"ref":"@mid"})");
        check(midpoint["kind"].asString() == "path-point" && midpoint.has("in") && midpoint.has("out"),
              "point get exposes world position and handles");
        check(ok(call(lv, "undo"), "undo insertion"), "undo mixed path edit");
        check(show(call(lv, "get", R"({"ref":"@editable"})")) == show(original), "undo restores point order and neighbor handles");
        check(ok(call(lv, "redo"), "redo insertion"), "redo mixed path edit");
        check(show(call(lv, "get", R"({"ref":"@editable"})")) == show(rail), "redo restores insertion order");
        check(ok(call(lv, "apply", R"({"ops":[{"op":"move-point","ref":"@mid","by":[10,20,30]}]})"), "move point"), "move point canonically");
        const Value moved = call(lv, "get", R"({"ref":"@mid"})");
        for (const char* key : {"pos", "in", "out"}) {
            for (int axis = 0; axis < 3; ++axis)
                check(std::fabs(moved[key].items()[axis].asNumber() - midpoint[key].items()[axis].asNumber() - (axis + 1) * 10) < .1,
                      "point and both handles move by the world delta");
        }
        check(ok(call(lv, "apply", R"({"ops":[{"op":"move-handle","ref":"@mid","handle":"out","by":[40,0,0]}]})"), "move handle"),
              "move outgoing handle");
        const Value handle = call(lv, "get", R"({"ref":"@mid"})");
        check(show(handle["in"]) == show(moved["in"]) &&
                  std::fabs(handle["out"].items()[0].asNumber() - moved["out"].items()[0].asNumber() - 40) < .1,
              "moving one handle preserves the other");
        const auto beforeFailure = lv.journal();
        Value deletionArgs = Value::object(), deletionOps = Value::array();
        deletionOps.push(parse(R"({"op":"delete-point","ref":"@mid"})"));
        Value deleteAnother = Value::object();
        deleteAnother.set("op", "delete-point");
        deleteAnother.set("ref", rail["points"].items()[1]["ref"]);
        deletionOps.push(deleteAnother);
        deletionArgs.set("ops", deletionOps);
        Value failure = lv.call("apply", deletionArgs);
        check(failure["error"]["code"].asString() == "op-failed" && show(lv.journal()) == show(beforeFailure),
              "minimum-point failure rolls the entire batch back");
        check(call(lv, "apply", R"({"ops":[{"op":"move-handle","ref":"@mid","handle":"side","by":[1,0,0]}]})")["error"]["code"].asString() ==
                  "bad-value",
              "unknown handle is an actionable error");
        check(call(lv, "apply", R"({"ops":[{"op":"insert-point","path":"@editable","segment":0,"t":0}]})")["error"]["code"].asString() == "bad-value",
              "insert t outside canonical interval refused");
        Value distance = call(
            lv, "apply",
            R"({"ops":[{"op":"path","points":[[0,2000,0],[900,2000,0],[900,2100,0]],"smooth":false,"as":"distance"},{"op":"array","class":"Coin","along":"@distance","spacing":200,"as":"sample"}]})");
        check(ok(distance, "rail array") && distance["created"].items().size() == 7, "arc-distance array includes the rail plus six coins");
        for (int i = 0; i < 6; ++i) {
            Value args = Value::object();
            args.set("ref", "@sample." + std::to_string(i));
            const auto point = lv.call("get", args)["pos"];
            check(std::fabs(point.items()[0].asNumber() - (i == 5 ? 900 : i * 200)) < .5 &&
                      std::fabs(point.items()[1].asNumber() - (i == 5 ? 2100 : 2000)) < .5,
                  "spacing remains K units across unequal rail segments");
        }
        auto copy = L::Level::open(options, &error);
        check(copy && copy->replay(lv.journal(), &error), "path journal replays: " + error.message);
        check(show(call(*copy, "get", R"({"ref":"@editable"})")) == show(call(lv, "get", R"({"ref":"@editable"})")),
              "journal preserves integer-valued float coordinates and ordered insertion");
        check(ok(call(lv, "save", R"({"mod":"Path Port"})"), "path save"), "save edited rails as patch");
        auto modOptions = options;
        modOptions.userMods = true;
        auto mod = L::Level::open(modOptions, &error);
        check(mod != nullptr, "reopen path patch: " + error.message);
        // Patch reload assigns original row refs; compare by rail ID and point positions through the raw model.
        bool matched = false;
        for (const auto& candidate : PetariEditor::Paths::collect(mod->model())) {
            if (std::string(mod->model().zoneName(candidate.zone)) != "DiskGardenZone")
                continue;
            if (candidate.id != rail["rail_id"].asInt())
                continue;
            const auto expected = PetariEditor::Paths::collect(lv.model());
            for (const auto& source : expected) {
                if (source.zone != candidate.zone || source.id != candidate.id)
                    continue;
                matched = candidate.points.size() == source.points.size();
                for (std::size_t i = 0; matched && i < source.points.size(); ++i) {
                    for (const auto pair : {std::pair{candidate.points[i].position, source.points[i].position},
                                           std::pair{candidate.points[i].incoming, source.points[i].incoming},
                                           std::pair{candidate.points[i].outgoing, source.points[i].outgoing}})
                        matched = matched && std::fabs(pair.first.x - pair.second.x) < .01 &&
                                  std::fabs(pair.first.y - pair.second.y) < .01 && std::fabs(pair.first.z - pair.second.z) < .01;
                }
            }
        }
        check(matched, "saved patch keeps point order, coordinates and both handles");
    }

}  // namespace

int main() {
    const Value liveHelp = L::Level::help(parse(R"({"live":"verify-route"})"));
    check(liveHelp["ok"].asBool() && liveHelp["live_command"]["schema"]["required"].items().size()==1,
          "live replay help exposes its request schema without opening a disc");
    const Value inputHelp = L::Level::help(parse(R"({"live":"inputs-start"})"));
    check(inputHelp["ok"].asBool() && inputHelp["live_command"]["schema"]["type"].asString() == "array", "frame-held input plan is discoverable");
    const fs::path disc = fs::path(PETARI_TEST_DISC_FILES).parent_path();
    if (!fs::exists(disc / "files" / "StageData" / "EggStarGalaxy" / "EggStarGalaxyScenario.arc")) {
        std::printf("petari_level: no disc at %s, skipped\n", disc.string().c_str());
        return 0;
    }
    const fs::path user = fs::temp_directory_path() / ("petari-petari-level-" + std::to_string(getpid()));
    fs::remove_all(user);

    L::OpenOptions options;
    options.disc = disc;
    options.user = user;
    options.galaxy = "EggStarGalaxy";
    options.mission = 1;

    if (const char* part = std::getenv("PETARI_LEVEL_API_TEST_PART")) {
        if (std::string(part) == "mod") {
            testModContinuation(options);
            fs::remove_all(user);
            std::printf("petari_level: %d mod checks passed\n", sChecks);
            return 0;
        }
        if (std::string(part) == "ground") {
            testGroundArrays(options);
            fs::remove_all(user);
            std::printf("petari_level ground arrays: %d checks passed\n", sChecks);
            return 0;
        }
    }

    // Open errors an agent can act on.
    {
        L::OpenOptions bad = options;
        bad.galaxy = "EggStar";
        L::Error e;
        check(!L::Level::open(bad, &e) && e.code == "unknown-galaxy", "unknown galaxy refused");
        bool suggested = false;
        for (const std::string& c : e.candidates)
            suggested = suggested || c == "EggStarGalaxy";
        check(suggested, "unknown galaxy suggests EggStarGalaxy");
    }

    auto t0 = std::chrono::steady_clock::now();
    L::Error error;
    std::unique_ptr< L::Level > level = L::Level::open(options, &error);
    check(level != nullptr, "open Good Egg: " + error.message);
    std::printf("open: %.0f ms\n", ms(t0));
    L::Level& lv = *level;

    const Value coordinateInfo = call(lv, "info");
    check(!coordinateInfo.has("existing_checks"), "existing checks are omitted from default replies");
    const Value diagnostics = call(lv, "info", R"({"existing_checks":true})");
    check(diagnostics.has("existing_checks") && diagnostics["existing_checks"].isArray(), "existing checks are explicitly available");
    check(diagnostics["existing_checks"].items().size() == call(lv, "checks", R"({"all":true})")["checks"].items().size(),
          "fresh-model existing checks match the core baseline");
    for (const auto& zone : coordinateInfo["zones"].items()) {
        const auto matrix = zone["world_matrix"];
        check(matrix.items().size() == 4 && matrix.items()[3].items()[3].asNumber() == 1, "zone matrix is homogeneous 4x4");
        const auto core = lv.model().zoneWorldMatrix(zone["index"].asInt());
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
                check(matrix.items()[row].items()[column].asNumber() == core.m[row][column], "zone matrix retains core precision");
    }
    const auto beforeDiagnosticsFailure = lv.journal();
    check(call(lv, "apply", R"({"existing_checks":"yes","ops":[{"op":"add","class":"Coin","pos":[0,2000,0]}]})")["error"]["code"].asString() == "bad-value" &&
          show(lv.journal()) == show(beforeDiagnosticsFailure), "bad diagnostic option cannot mutate the model");
    Value allocated = call(lv, "apply", R"({"dry_run":true,"ops":[{"op":"add","class":"Coin","pos":[0,2000,0],"switches":{"SW_A":"new","SW_B":"new"},"as":"switch_probe"}]})");
    check(ok(allocated, "switch allocation") && allocated["allocated_switches"].items().size() == 2, "mutation replies report both allocated switches");
    const auto allocations = allocated["allocated_switches"].items();
    check(allocations[0]["id"].asInt() != allocations[1]["id"].asInt() && allocations[0]["zone"].asString() == "EggStarGalaxy" &&
          allocations[0]["ref"].asString() == allocations[1]["ref"].asString(), "switch allocation reserves distinct IDs within one row");
    check(show(lv.journal()) == show(beforeDiagnosticsFailure), "dry-run switch reservation leaves no committed rows or labels");

    // Self-description.
    Value help = L::Level::help(Value::object());
    check(help["ok"].asBool() && help["commands"].items().size() >= 10 && help["ops"].items().size() >= 8, "help lists commands and ops");
    check(L::Level::help(parse(R"({"op":"add"})"))["op"]["schema"].isObject(), "help --op add has a schema");
    check(!L::Level::help(parse(R"({"op":"ad"})"))["ok"].asBool(), "help of an unknown op fails");

    // info, catalog, class.
    Value info = call(lv, "info");
    check(ok(info, "info") && info["zones"].items().size() > 1 && info["missions"].items().size() >= 3, "info: zones and missions");
    check(info["errors"].asInt() == 0 && info["warnings"].asInt() == 0, "the shipped galaxy reports no new problems");
    Value catalog = call(lv, "catalog", R"({"query":"coin"})");
    bool coin = false;
    for (const Value& c : catalog["classes"].items())
        coin = coin || c["class"].asString() == "Coin";
    check(coin, "catalog search finds Coin");
    Value goomba = call(lv, "class", R"({"class":"goomba"})");
    check(ok(goomba, "class goomba") && goomba["class"].asString() == "Kuribo", "a display name resolves to the class");
    Value typo = call(lv, "class", R"({"class":"Gooomba"})");
    check(!typo["ok"].asBool() && typo["error"]["code"].asString() == "unknown-class" && hasCandidate(typo, "Kuribo"), "a typo suggests Kuribo");

    // list and get.
    Value coins = call(lv, "list", R"({"class":"Coin"})");
    check(ok(coins, "list") && coins["total"].asInt() > 0, "Good Egg has coins");
    const std::string firstCoin = coins["objects"].items()[0]["ref"].asString();
    Value got = lv.call("get", parse(("{\"ref\":\"" + firstCoin + "\"}").c_str()));
    check(ok(got, "get") && got["class"].asString() == "Coin" && got["fields"].has("pos_x"), "get by ref");
    check(call(lv, "get", R"({"ref":"Coin#0"})")["ref"].asString() == firstCoin, "Class#n refs");
    check(call(lv, "get", R"({"ref":"Coin#9999"})")["error"]["code"].asString() == "bad-ref", "Class#n past the end");
    check(call(lv, "get", R"({"ref":{"class":"Coin"}})")["error"]["code"].asString() == "ambiguous-ref", "an ambiguous selector");

    // A batch: add with named args and a label, a duplicate of it; one undo step.
    const int historyBefore = call(lv, "history")["entries"].items().size();
    t0 = std::chrono::steady_clock::now();
    Value applied = call(lv, "apply", R"({"key":"star-1","ops":[
        {"op":"add","class":"Power Star","pos":[100,2000,-300],"missions":[2],"args":{"Star ID":1,"Rise along gravity":true},"as":"star"},
        {"op":"add","class":"Coin","pos":[0,1800,0],"as":"c1"},
        {"op":"duplicate","ref":"@c1","by":[200,0,0],"as":"c2"}]})");
    std::printf("apply 3 ops: %.0f ms\n", ms(t0));
    check(ok(applied, "apply") && applied["created"].items().size() == 3, "three rows created");
    check(static_cast< int >(call(lv, "history")["entries"].items().size()) == historyBefore + 1, "one batch = one journal entry");
    Value star = call(lv, "get", R"({"ref":"@star"})");
    check(star["class"].asString() == "PowerStar" && star["args"]["Star ID"].asInt() == 1 && star["fields"]["Obj_arg1"].asInt() == 1,
          "named args written");
    check(std::fabs(star["pos"].items()[1].asNumber() - 2000) < 0.5, "world position kept");
    Value c2 = call(lv, "get", R"({"ref":"@c2"})");
    check(std::fabs(c2["pos"].items()[0].asNumber() - 200) < 0.5, "duplicate moved by");

    // Idempotent: the same key is skipped.
    Value again = call(lv, "apply", R"({"key":"star-1","ops":[{"op":"add","class":"Coin","pos":[0,0,0]}]})");
    check(ok(again, "again") && again["skipped"].asBool(), "an applied key is skipped");

    // Atomic: a bad op later in the batch leaves nothing behind.
    const int objects = call(lv, "info")["objects"].asInt();
    Value bad = call(lv, "apply", R"({"ops":[{"op":"add","class":"Coin","pos":[0,0,0]},{"op":"set","ref":"@star","args":{"Star IDD":2}}]})");
    check(!bad["ok"].asBool() && bad["error"]["code"].asString() == "bad-arg" && hasCandidate(bad, "Star ID"),
          "a bad arg name suggests the real one");
    check(bad["error"]["where"].asString().rfind("ops[1]", 0) == 0, "the error says which op");
    check(call(lv, "info")["objects"].asInt() == objects, "a failed batch changes nothing");
    Value badKey = call(lv, "apply", R"({"ops":[{"op":"add","class":"Coin","position":[0,0,0]}]})");
    check(badKey["error"]["code"].asString() == "bad-args" && hasCandidate(badKey, "pos"), "an unknown key suggests pos");
    Value badEnum = call(lv, "apply", R"({"ops":[{"op":"set","ref":"@c1","args":{"Shadow type":"Squiggle"}}]})");
    check(badEnum["error"]["code"].asString() == "bad-value" && hasCandidate(badEnum, "Surface circle"), "a bad enum label lists the choices");

    // Dry run: reports, changes nothing.
    Value dry = call(lv, "apply", R"({"dry_run":true,"ops":[{"op":"delete","ref":"@c1"}]})");
    check(ok(dry, "dry") && dry["dry_run"].asBool() && dry["deleted"].items().size() == 1, "dry run reports the delete");
    check(call(lv, "get", R"({"ref":"@c1"})")["ok"].asBool(), "dry run kept the row");

    // Checks: an out-of-range switch is a new error with a fix; the fix op resolves it.
    Value broken = call(lv, "apply", R"({"ops":[{"op":"set","ref":"@star","switches":{"SW_APPEAR":300}}]})");
    check(ok(broken, "switch 300") && broken["checks_introduced"].items().size() == 1, "the batch reports the check it introduced");
    const std::string key = broken["checks_introduced"].items()[0]["key"].asString();
    check(call(lv, "checks")["errors"].asInt() == 1, "checks: one new error");
    // A placed Power Star reusing its mission's Star ID is an error (after a restart only one exists), with a fix to a free ID.
    // (The star above is in mission 2 only, where Star ID 1 is free; mission 2's own star is ID 2.)
    Value dupe = call(lv, "apply", R"({"dry_run":true,"ops":[{"op":"add","class":"PowerStar","pos":[300,2000,-300],"missions":[2],"args":{"Star ID":2}}]})");
    bool dupeError = false;
    for (const Value& c : dupe["checks_introduced"].items()) {
        dupeError = dupeError || (c["key"].asString() == "duplicate-star:2:2" && c["severity"].asString() == "error" &&
                                  c["fix"].asString().rfind("Use Star ID ", 0) == 0);
    }
    check(ok(dupe, "duplicate star") && dupeError, "a placed duplicate Star ID is an error with a 'Use Star ID n' fix");
    Value fixed = lv.call("apply", parse(("{\"ops\":[{\"op\":\"fix\",\"check\":\"" + key + "\"}]}").c_str()));
    check(ok(fixed, "fix") && fixed["checks_resolved"].items().size() == 1 && call(lv, "checks")["errors"].asInt() == 0, "the fix resolves it");
    Value fresh = call(lv, "apply", R"({"ops":[{"op":"set","ref":"@star","switches":{"SW_APPEAR":"new"}}]})");
    const int switchId = call(lv, "get", R"({"ref":"@star"})")["switches"]["SW_APPEAR"].asInt(-1);
    check(ok(fresh, "new switch") && switchId >= 0 && switchId < 128, "\"new\" picks a free zone switch");

    // Undo / redo by batch.
    const int cursor = call(lv, "history")["cursor"].asInt();
    check(ok(call(lv, "undo"), "undo") && call(lv, "history")["cursor"].asInt() == cursor - 1, "undo one batch");
    check(ok(call(lv, "redo"), "redo") && call(lv, "history")["cursor"].asInt() == cursor, "redo it");

    // A zone row: world <-> zone space.
    std::string zoneName;
    for (const Value& z : info["zones"].items()) {
        if (z["index"].asInt() > 0 && z["placed"].asBool() && zoneName.empty())
            zoneName = z["name"].asString();
    }
    check(!zoneName.empty(), "Good Egg has a placed sub-zone");
    Value inZone = lv.call(
        "apply",
        parse(("{\"ops\":[{\"op\":\"add\",\"class\":\"Coin\",\"zone\":\"" + zoneName + "\",\"pos\":[1000,2000,3000],\"as\":\"zc\"}]}").c_str()));
    check(ok(inZone, "zone add"), "add in a zone");
    Value zc = call(lv, "get", R"({"ref":"@zc"})");
    check(std::fabs(zc["pos"].items()[0].asNumber() - 1000) < 0.5 && std::fabs(zc["pos"].items()[2].asNumber() - 3000) < 0.5 && zc.has("zone_pos"),
          "a zone row's world position round-trips");

    // Placement: the ground below Mario's mission-1 start (Disk Garden), along its disk gravity.
    Value start = call(lv, "get", R"({"ref":{"class":"Mario","zone":"EggStarGalaxy","table":"start/layera"}})");
    check(ok(start, "start"), "the mission-1 start");
    const double sx = start["pos"].items()[0].asNumber(), sy = start["pos"].items()[1].asNumber(), sz = start["pos"].items()[2].asNumber();
    char buf[512];
    std::snprintf(buf, sizeof(buf), R"({"pos":[%.1f,%.1f,%.1f]})", sx, sy + 600, sz);
    Value ground = lv.call("ground", parse(buf));
    check(ok(ground, "ground") && ground["hit"].asBool() && ground["on_class"].asString() == "DiskGardenPlanet",
          "ground under the start is Disk Garden");
    check(ground["gravity"].asString().find("GlobalDiskGravity") == 0, "the disk's gravity there");
    check(std::fabs(ground["point"].items()[1].asNumber() - sy) < 40, "the ground is where Mario starts (within 40 units)");
    // A point inside the planet is refused (the evaluator's underside bug), naming the collider and how far to raise it.
    std::snprintf(buf, sizeof(buf), R"({"ops":[{"op":"add","class":"Coin","pos":[%.1f,%.1f,%.1f],"on_ground":true}]})", sx, sy - 300, sz);
    Value inside = lv.call("apply", parse(buf));
    check(inside["error"]["code"].asString() == "inside-collision" && hasCandidate(inside, "DiskGardenZone:common/objinfo:11"),
          "on_ground from inside the planet: inside-collision naming Disk Garden");
    check(inside["error"]["message"].asString().find("units below its surface") != std::string::npos, "it says how far below the surface");
    std::snprintf(buf, sizeof(buf), R"({"pos":[%.1f,%.1f,%.1f]})", sx, sy - 300, sz);
    Value insideProbe = lv.call("ground", parse(buf));
    check(insideProbe["hit"].asBool() == false && insideProbe.has("inside") && insideProbe["surface_above"].asNumber() > 200,
          "ground reports inside + surface_above");
    // 50 units inside is an error too (strict: only numerical slack is forgiven).
    std::snprintf(buf, sizeof(buf), R"({"dry_run":true,"ops":[{"op":"add","class":"Coin","pos":[%.1f,%.1f,%.1f],"on_ground":true}]})", sx,
                  ground["point"].items()[1].asNumber() - 50, sz);
    check(lv.call("apply", parse(buf))["error"]["code"].asString() == "inside-collision", "50 units inside the starter planet: inside-collision");
    // From above, it lands on the top.
    std::snprintf(buf, sizeof(buf), R"({"ops":[{"op":"add","class":"Coin","pos":[%.1f,%.1f,%.1f],"on_ground":true,"height":100,"as":"gc"}]})", sx,
                  sy + 50, sz);
    Value grounded = lv.call("apply", parse(buf));
    check(ok(grounded, "on_ground") && grounded["placements"].items().size() == 1, "on_ground reports its placement");
    const double gy = call(lv, "get", R"({"ref":"@gc"})")["pos"].items()[1].asNumber();
    check(std::fabs(gy - (ground["point"].items()[1].asNumber() + 100)) < 40, "on_ground + height 100 sits 100 above the top surface");
    check(call(lv, "get", R"({"ref":"@gc"})")["up"].items()[1].asNumber() > 0.9, "on_ground stands the object up against gravity");
    Value noGround = call(lv, "apply", R"({"ops":[{"op":"add","class":"Coin","pos":[0,90000,0],"on_ground":true,"down":[0,1,0]}]})");
    check(noGround["error"]["code"].asString() == "no-ground", "no ground: an actionable error");

    // Arrays: an arc of 7 coins with labels; a ring; a grid.
    std::snprintf(
        buf, sizeof(buf),
        R"({"ops":[{"op":"array","class":"Coin","shape":"arc","from":[%.1f,%.1f,%.1f],"to":[%.1f,%.1f,%.1f],"peak":400,"count":7,"down":[0,-1,0],"as":"arc"}]})",
                  sx, sy + 100, sz, sx + 1200, sy + 100, sz);
    Value arc = lv.call("apply", parse(buf));
    check(ok(arc, "arc") && arc["created"].items().size() == 7, "an arc of 7");
    const double mid = call(lv, "get", R"({"ref":"@arc.3"})")["pos"].items()[1].asNumber();
    check(std::fabs(mid - (sy + 500)) < 1, "the arc's middle is peak above the line");
    std::snprintf(
        buf, sizeof(buf),
        R"({"ops":[{"op":"array","class":"StarPiece","shape":"grid","origin":[%.1f,%.1f,%.1f],"size":[3,2],"spacing":100,"down":[0,-1,0]},{"op":"array","class":"Coin","shape":"ring","center":[%.1f,%.1f,%.1f],"radius":300,"count":6,"down":[0,-1,0]}]})",
                  sx, sy + 800, sz, sx, sy + 900, sz);
    Value shapes = lv.call("apply", parse(buf));
    check(ok(shapes, "grid+ring") && shapes["created"].items().size() == 12, "a 3x2 grid and a ring of 6 in one batch");

    // Switch references: the mushroom reads the Goomba's fresh switch.
    std::snprintf(
        buf, sizeof(buf),
        R"({"ops":[{"op":"add","class":"Kuribo","pos":[%.1f,%.1f,%.1f],"switches":{"SW_DEAD":"new"},"as":"goomba"},{"op":"add","class":"BenefitItemOneUp","pos":[%.1f,%.1f,%.1f],"switches":{"SW_APPEAR":"@goomba.SW_DEAD"},"as":"oneup"}]})",
                  sx + 300, sy, sz, sx + 600, sy, sz);
    Value linked = lv.call("apply", parse(buf));
    check(ok(linked, "linked switches"), "add a Goomba and a mushroom linked by a switch");
    check(call(lv, "get", R"({"ref":"@goomba"})")["switches"]["SW_DEAD"].asInt() ==
              call(lv, "get", R"({"ref":"@oneup"})")["switches"]["SW_APPEAR"].asInt(),
          "the mushroom reads the Goomba's switch");
    check(linked["checks_introduced"].items().size() == 0, "a written-and-read switch is no problem");
    Value lonely = call(lv, "apply", R"({"dry_run":true,"ops":[{"op":"set","ref":"@oneup","switches":{"SW_APPEAR":"new"}}]})");
    check(lonely["checks_introduced"].items().size() >= 1, "a switch nothing writes is reported");

    // Paths: a smooth 3-point rail, and a Launch Star added on it.
    std::snprintf(
        buf, sizeof(buf),
        R"({"ops":[{"op":"path","points":[[%.1f,%.1f,%.1f],[%.1f,%.1f,%.1f],[%.1f,%.1f,%.1f]],"as":"flight"},{"op":"add","class":"SuperSpinDriver","pos":[%.1f,%.1f,%.1f],"path":"@flight","as":"launch"}]})",
                  sx, sy + 200, sz, sx + 1500, sy + 2500, sz, sx + 3000, sy + 200, sz, sx, sy + 200, sz);
    Value flight = lv.call("apply", parse(buf));
    check(ok(flight, "path"), "a path and a Launch Star on it");
    Value launch = call(lv, "get", R"({"ref":"@launch"})");
    Value rail = call(lv, "get", R"({"ref":"@flight"})");
    check(launch["links"]["CommonPath_ID"].asInt(-1) == rail["fields"]["l_id"].asInt(-2) && rail["fields"]["num_pnt"].asInt() == 3,
          "the Launch Star flies the new path");
    check(rail["points"].items().size() == 3 && std::fabs(rail["points"].items()[1]["pos"].items()[1].asNumber() - (sy + 2500)) < 1,
          "get on a rail lists its points in world space");
    check(rail["points"].items()[1]["in"].items()[0].asNumber() < rail["points"].items()[1]["out"].items()[0].asNumber(),
          "smooth handles by default");
    check(flight["created"].items()[0]["points"].asInt() == 3 && flight["created"].items()[0].has("pos"), "a created rail summarises its points");
    std::snprintf(
        buf, sizeof(buf),
        R"({"dry_run":true,"ops":[{"op":"array","class":"Coin","shape":"line","start":[%.1f,%.1f,%.1f],"end":[%.1f,%.1f,%.1f],"count":4,"down":[0,-1,0]}]})",
                  sx, sy + 900, sz, sx + 600, sy + 900, sz);
    check(lv.call("apply", parse(buf))["created"].items().size() == 4, "array start/end (from/to stay aliases)");
    bool linkProblem = false;
    for (const Value& c : flight["checks_introduced"].items())
        linkProblem = linkProblem || c["key"].asString().find("link") != std::string::npos;
    check(!linkProblem, "a linked Launch Star raises no link check");
    bool facing = false;
    for (const Value& c : flight["checks_introduced"].items()) facing = facing || c["key"].asString().rfind("launch-star-facing", 0) == 0;
    check(!facing, "a Launch Star added on a path faces along it (no launch-star-facing check)");
    std::snprintf(buf, sizeof(buf), R"({"dry_run":true,"ops":[{"op":"add","class":"SuperSpinDriver","pos":[%.1f,%.1f,%.1f],"path":"@flight","face_path":false}]})",
                  sx, sy + 200, sz);
    Value unfaced = lv.call("apply", parse(buf));
    facing = false;
    for (const Value& c : unfaced["checks_introduced"].items()) facing = facing || c["key"].asString().rfind("launch-star-facing", 0) == 0;
    check(facing, "face_path false leaves it facing away, and the core check says so");
    Value unlinked = call(lv, "apply", R"({"dry_run":true,"ops":[{"op":"add","class":"SuperSpinDriver","pos":[0,0,0]}]})");
    check(unlinked["checks_introduced"].items().size() >= 1, "a Launch Star without a path is reported");

    // The journal replays into a fresh level with the same rows and labels.
    const Value journal = lv.journal();
    {
        std::unique_ptr< L::Level > copy = L::Level::open(options, &error);
        check(copy && copy->replay(journal, &error), "replay: " + error.message);
        check(show(call(*copy, "get", R"({"ref":"@star"})")) == show(call(lv, "get", R"({"ref":"@star"})")), "replayed star identical");
        check(call(*copy, "info")["objects"].asInt() == call(lv, "info")["objects"].asInt(), "replayed object count");
    }

    // The op format round-trips.
    Value exported = call(lv, "export");
    check(ok(exported, "export") && exported["ops"].items().size() == journal["entries"].items().size(), "export lists the journal's ops");

    // Save a patch and load it back as the base (the game's view).
    Value saved = call(lv, "save", R"({"mod":"Api Test"})");
    check(ok(saved, "save") && fs::exists(user / "mods" / "Api Test" / "edits" / "EggStarGalaxy.json"), "patch saved");
    {
        L::OpenOptions withMods = options;
        withMods.userMods = true;
        std::unique_ptr< L::Level > modded = L::Level::open(withMods, &error);
        check(modded != nullptr, "open with the saved mod: " + error.message);
        Value stars = call(*modded, "list", R"({"class":"PowerStar","near":[100,2000,-300],"radius":5})");
        check(stars["total"].asInt() == 1, "the saved star is in the modded galaxy");
    }

    testModContinuation(options);
    testPathEdits(options);
    testGravityEdits(options);
    testGroundArrays(options);
    fs::remove_all(user);
    std::printf("petari_level: %d checks passed\n", sChecks);
    return 0;
}
