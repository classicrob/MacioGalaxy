// Tests for the agent level-authoring API (native/editor/level_api, petari-level-x) on Good Egg from the
// extracted disc: queries, batches as one undo step, atomicity, idempotent keys, named args, refs,
// errors with candidates, the journal's replay, checks and their fixes, and a patch save loaded back.
// Skipped without the disc.
#include <petari/editor/level_api.hpp>

#include "edit_model.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    if (!reply["ok"].asBool()) std::fprintf(stderr, "%s: %s\n", what.c_str(), show(reply).c_str());
    return reply["ok"].asBool();
}

bool hasCandidate(const Value& reply, const std::string& text) {
    for (const Value& c : reply["error"]["candidates"].items()) {
        if (c.asString().find(text) != std::string::npos) return true;
    }
    return false;
}

double ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - since).count();
}
}  // namespace

int main() {
    const fs::path disc = fs::path(PETARI_TEST_DISC_FILES).parent_path();
    if (!fs::exists(disc / "files" / "StageData" / "EggStarGalaxy" / "EggStarGalaxyScenario.arc")) {
        std::printf("level_api_x: no disc at %s, skipped\n", disc.string().c_str());
        return 0;
    }
    const fs::path user = fs::temp_directory_path() / ("petari-level-api-x-" + std::to_string(getpid()));
    fs::remove_all(user);

    L::OpenOptions options;
    options.disc = disc;
    options.user = user;
    options.galaxy = "EggStarGalaxy";
    options.mission = 1;

    // Open errors an agent can act on.
    {
        L::OpenOptions bad = options;
        bad.galaxy = "EggStar";
        L::Error e;
        check(!L::Level::open(bad, &e) && e.code == "unknown-galaxy", "unknown galaxy refused");
        bool suggested = false;
        for (const std::string& c : e.candidates) suggested = suggested || c == "EggStarGalaxy";
        check(suggested, "unknown galaxy suggests EggStarGalaxy");
    }

    auto t0 = std::chrono::steady_clock::now();
    L::Error error;
    std::unique_ptr< L::Level > level = L::Level::open(options, &error);
    check(level != nullptr, "open Good Egg: " + error.message);
    std::printf("open: %.0f ms\n", ms(t0));
    L::Level& lv = *level;

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
    for (const Value& c : catalog["classes"].items()) coin = coin || c["class"].asString() == "Coin";
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
        {"op":"add","class":"Power Star","pos":[100,2000,-300],"args":{"Star ID":1,"Rise along gravity":true},"as":"star"},
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
    check(!bad["ok"].asBool() && bad["error"]["code"].asString() == "bad-arg" && hasCandidate(bad, "Star ID"), "a bad arg name suggests the real one");
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
        if (z["index"].asInt() > 0 && z["placed"].asBool() && zoneName.empty()) zoneName = z["name"].asString();
    }
    check(!zoneName.empty(), "Good Egg has a placed sub-zone");
    Value inZone = lv.call("apply", parse(("{\"ops\":[{\"op\":\"add\",\"class\":\"Coin\",\"zone\":\"" + zoneName +
                                           "\",\"pos\":[1000,2000,3000],\"as\":\"zc\"}]}").c_str()));
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
    check(ok(ground, "ground") && ground["hit"].asBool() && ground["on_class"].asString() == "DiskGardenPlanet", "ground under the start is Disk Garden");
    check(ground["gravity"].asString().find("GlobalDiskGravity") == 0, "the disk's gravity there");
    check(std::fabs(ground["point"].items()[1].asNumber() - sy) < 40, "the ground is where Mario starts (within 40 units)");
    // A point just inside the planet still lands on its top (back faces are skipped).
    std::snprintf(buf, sizeof(buf), R"({"ops":[{"op":"add","class":"Coin","pos":[%.1f,%.1f,%.1f],"on_ground":true,"height":100,"as":"gc"}]})", sx, sy - 30, sz);
    Value grounded = lv.call("apply", parse(buf));
    check(ok(grounded, "on_ground") && grounded["placements"].items().size() == 1, "on_ground reports its placement");
    const double gy = call(lv, "get", R"({"ref":"@gc"})")["pos"].items()[1].asNumber();
    check(std::fabs(gy - (ground["point"].items()[1].asNumber() + 100)) < 40, "on_ground + height 100 sits 100 above the top surface");
    check(call(lv, "get", R"({"ref":"@gc"})")["up"].items()[1].asNumber() > 0.9, "on_ground stands the object up against gravity");
    Value noGround = call(lv, "apply", R"({"ops":[{"op":"add","class":"Coin","pos":[0,90000,0],"on_ground":true,"down":[0,1,0]}]})");
    check(noGround["error"]["code"].asString() == "no-ground", "no ground: an actionable error");

    // Arrays: an arc of 7 coins with labels; a ring; a grid.
    std::snprintf(buf, sizeof(buf), R"({"ops":[{"op":"array","class":"Coin","shape":"arc","from":[%.1f,%.1f,%.1f],"to":[%.1f,%.1f,%.1f],"peak":400,"count":7,"down":[0,-1,0],"as":"arc"}]})",
                  sx, sy + 100, sz, sx + 1200, sy + 100, sz);
    Value arc = lv.call("apply", parse(buf));
    check(ok(arc, "arc") && arc["created"].items().size() == 7, "an arc of 7");
    const double mid = call(lv, "get", R"({"ref":"@arc.3"})")["pos"].items()[1].asNumber();
    check(std::fabs(mid - (sy + 500)) < 1, "the arc's middle is peak above the line");
    std::snprintf(buf, sizeof(buf), R"({"ops":[{"op":"array","class":"StarPiece","shape":"grid","origin":[%.1f,%.1f,%.1f],"size":[3,2],"spacing":100,"down":[0,-1,0]},{"op":"array","class":"Coin","shape":"ring","center":[%.1f,%.1f,%.1f],"radius":300,"count":6,"down":[0,-1,0]}]})",
                  sx, sy + 800, sz, sx, sy + 900, sz);
    Value shapes = lv.call("apply", parse(buf));
    check(ok(shapes, "grid+ring") && shapes["created"].items().size() == 12, "a 3x2 grid and a ring of 6 in one batch");

    // Switch references: the mushroom reads the Goomba's fresh switch.
    std::snprintf(buf, sizeof(buf), R"({"ops":[{"op":"add","class":"Kuribo","pos":[%.1f,%.1f,%.1f],"switches":{"SW_DEAD":"new"},"as":"goomba"},{"op":"add","class":"BenefitItemOneUp","pos":[%.1f,%.1f,%.1f],"switches":{"SW_APPEAR":"@goomba.SW_DEAD"},"as":"oneup"}]})",
                  sx + 300, sy, sz, sx + 600, sy, sz);
    Value linked = lv.call("apply", parse(buf));
    check(ok(linked, "linked switches"), "add a Goomba and a mushroom linked by a switch");
    check(call(lv, "get", R"({"ref":"@goomba"})")["switches"]["SW_DEAD"].asInt() == call(lv, "get", R"({"ref":"@oneup"})")["switches"]["SW_APPEAR"].asInt(),
          "the mushroom reads the Goomba's switch");
    check(linked["checks_introduced"].items().size() == 0, "a written-and-read switch is no problem");
    Value lonely = call(lv, "apply", R"({"dry_run":true,"ops":[{"op":"set","ref":"@oneup","switches":{"SW_APPEAR":77}}]})");
    check(lonely["checks_introduced"].items().size() >= 1, "a switch nothing writes is reported");

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

    fs::remove_all(user);
    std::printf("level_api_x: %d checks passed\n", sChecks);
    return 0;
}
