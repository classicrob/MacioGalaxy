// Tests for the level editor's catalog (petari/editor/catalog.hpp) and its JSON reader/writer:
// the embedded files load, every factory class is present, curated entries override generated
// ones, search ranks names, user files merge and broken ones are skipped. No game, no disc.
#include "petari/editor/catalog.hpp"
#include "petari/editor/json.hpp"
#include "petari/editor/terrain/brush_catalog.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace C = PetariEditor::Catalog;
namespace J = PetariEditor::Json;
namespace fs = std::filesystem;

namespace {
int checks = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
        std::exit(1);
    }
}

void testJson() {
    std::string error;
    auto v = J::parse(R"({"a": [1, 2.5, "xé\n", true, null], "b": {"c": -3}})", &error);
    check(v && v->isObject() && (*v)["a"].items().size() == 5, "parse: " + error);
    check((*v)["a"].items()[1].asNumber() == 2.5 && (*v)["a"].items()[2].asString() == "x\xc3\xa9\n", "numbers and escapes");
    check((*v)["b"]["c"].asInt() == -3 && (*v)["missing"].isNull(), "nested and missing");
    auto again = J::parse(J::write(*v, true));
    check(again && J::write(*again) == J::write(*v), "write round-trips");
    check(!J::parse("{\"a\": [1, 2}", &error) && !error.empty(), "syntax errors reported");
    check(!J::parse("[1] x"), "trailing text refused");
}

void testCatalog() {
    const fs::path user = fs::temp_directory_path() / "petari-catalog-test";
    fs::remove_all(user);
    fs::create_directories(user / "editor" / "catalog");
    std::ofstream(user / "editor" / "catalog" / "names.json")
        << R"({"classes": [{"name": "Kuribo", "aliases": ["mushroom guy"], "args": [{"index": 1, "name": "Patrol radius", "help": "units"}]},
                           {"name": "NoSuchClassAnywhere", "displayName": "nope"}]})";
    std::ofstream(user / "editor" / "catalog" / "broken.json") << "{ not json";
    std::ofstream(user / "editor" / "catalog" / "presets.json")
        << R"({"classes": [{"name": "Coin", "presets": [{"label": "Spinning", "args": {"0": 1}}, {"label": "Still", "args": {"0": -1}, "scale": [2, 0.5, 2]}]}]})";
    std::string error;
    check(C::loadWithUser(user.string(), &error), "loads: " + error);
    check(C::classes().size() > 1100, "every factory class (" + std::to_string(C::classes().size()) + ")");
    const C::ClassInfo* kuribo = C::find("kuribo");
    check(kuribo != nullptr && kuribo->category == C::Category::Enemy, "found case-insensitively, category from the scan");
    check(!kuribo->switches.empty() && !kuribo->archives.empty(), "switches and archives from the scan");
    bool radius = false;
    for (const C::Arg& a : kuribo->args) radius = radius || (a.index == 1 && a.name == "Patrol radius" && a.help == "units");
    check(radius, "a user file names an arg");
    check(C::find("NoSuchClassAnywhere") == nullptr, "user files don't invent classes");
    bool alias = false;
    for (const C::ClassInfo* c : C::search("mushroom")) alias = alias || c == kuribo;
    check(alias, "search finds aliases");
    check(!C::search("Kuri").empty() && C::search("zzzzzz").empty(), "search");
    check(std::string(C::categoryName(C::Category::Gravity)) == "Gravity", "category names");
    const C::ClassInfo* coin = C::find("Coin");
    check(coin && coin->presets.size() == 2 && coin->presets[0].label == "Spinning" && coin->presets[0].args[0] == std::make_pair(0, 1),
          "presets from a file");
    check(coin->presets[0].scale[1] == 1.0f && coin->presets[1].scale[0] == 2.0f && coin->presets[1].scale[1] == 0.5f, "a preset's size");
    C::ClassInfo brush;
    brush.name = "EditorBrush";
    brush.displayName = "Terrain brush";
    brush.category = C::Category::Planet;
    brush.presets = {{"Box", {{0, 0}}}, {"Sphere", {{0, 1}}}};
    C::addClass(brush);
    check(C::find("editorbrush") && C::find("EditorBrush")->presets.size() == 2, "addClass");
    brush.description = "replaced";
    C::addClass(brush);
    check(C::find("EditorBrush")->description == "replaced", "addClass replaces");
    // Where new rows go: the disc's table with the layer put in; by category when never placed.
    check(C::tablePath(*C::find("Kuribo"), "common") == "jmp/placement/common/objinfo", "Goomba: objinfo");
    check(C::find("RailMoveObj") == nullptr || C::tablePath(*C::find("RailMoveObj"), "layera") == "jmp/mapparts/layera/mappartsinfo",
          "map parts: jmp/mapparts");
    check(C::tablePath(*C::find("EditorBrush"), "common") == "jmp/placement/common/objinfo", "not on the disc: objinfo");
    bool readsA = false, writesA = false;
    for (const auto& socket : C::find("BroadBeanMoveStepA")->switches)
        if (socket.field == "SW_A") {
            readsA = readsA || socket.role == C::SwitchRole::Read;
            writesA = writesA || socket.role == C::SwitchRole::Write;
        }
    check(readsA && writesA, "switch fields retain both read and write roles");
    // Curated entries (native/editor/catalog/curated.json) name the common classes.
    check(kuribo->displayName == "Goomba" && kuribo->curated && !kuribo->description.empty(), "Kuribo is the Goomba");
    check(C::find("Takobo")->displayName == "Octoomba" && C::find("SuperSpinDriver")->displayName == "Launch Star", "curated names");
    check(C::find("SuperSpinDriver")->category == C::Category::Launcher, "curated category");
    check(!C::search("goomba").empty() && C::search("goomba").front() == kuribo, "search by display name");
    fs::remove_all(user);
}
}  // namespace

// The terrain brush's class, built without the runtime (headless tools register it too).
static void testBrushClass() {
    const C::ClassInfo info = PetariEditor::Terrain::brushClassInfo();
    check(info.name == "EditorBrush" && info.placeable && info.args.size() == 4 && info.presets.size() == 4, "brush class: 4 args, 4 presets");
    check(info.args.size() == 4 && info.args[3].choices.size() == 3 && info.args[3].defaultValue == 1, "brush gravity: 3 modes, fitted by default");
    check(info.presets.size() == 4 && info.presets[0].scale[1] < 0.2f && info.presets[1].scale[0] > 1.0f, "brush presets: human-scale sizes");
    PetariEditor::Terrain::registerBrushClass();
    check(C::find("EditorBrush") != nullptr, "brush class registered");
}

int main() {
    testJson();
    testCatalog();
    testBrushClass();
    {
        // The collision box from the disc's KCL: StrongBlock is a 150 cube anchored at its bottom centre.
        const C::ClassInfo* pBlock = C::find("StrongBlock");
        check(pBlock && pBlock->hasCollisionBox && pBlock->collisionBox[1] > 0.0f && pBlock->collisionBox[1] < 1.0f &&
                  pBlock->collisionBox[4] > 149.0f && pBlock->collisionBox[3] > 74.0f,
              "StrongBlock's collision box");
    }
    std::printf("editor catalog: %d checks passed (%zu classes)\n", checks, C::classes().size());
    return 0;
}
