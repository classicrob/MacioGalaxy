// The mod galaxy registry (native/platform/dvd/mod_galaxy.cpp): galaxy.json parsing, messages by
// language with the English fallback, music borrowing, rejection, the save key, and loading from
// mod folders. Links: petari_platform_dvd.
#include <petari/platform/mod_galaxy.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace G = PetariNative::Platform::ModGalaxy;

static int sFailures = 0;
static void check(bool condition, const char* text) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", text);
        ++sFailures;
    }
}

static std::u16string u(const char16_t* p) { return p != nullptr ? std::u16string(p) : std::u16string(u"(null)"); }

static void testParse() {
    G::clear();
    std::string error;
    const char* json = R"({ "format": 1, "galaxies": [ {
        "name": "ModTestGalaxy",
        "title": { "en": "Blockout Test Galaxy", "fr": "Galaxie d'essai é" },
        "missions": [ { "en": "The Checkered Box" }, "Second" ],
        "music": "EggStarGalaxy" } ] })";
    check(G::loadText(json, "GalaxyTest", &error), "a valid galaxy.json loads");
    const G::Galaxy* pGalaxy = G::find("ModTestGalaxy");
    check(pGalaxy != nullptr && pGalaxy->mod == "GalaxyTest" && pGalaxy->missions.size() == 2, "the galaxy is registered with its missions");
    check(G::find("modtestgalaxy") == nullptr, "names are case-sensitive, like the game's tables");
    check(u(G::message("GalaxyName_ModTestGalaxy", "en")) == u"Blockout Test Galaxy", "galaxy name in English");
    check(u(G::message("GalaxyName_ModTestGalaxy", "fr")) == u"Galaxie d'essai é", "galaxy name in French (UTF-8 and \\u escapes)");
    check(u(G::message("GalaxyName_ModTestGalaxy", "es")) == u"Blockout Test Galaxy", "a missing language falls back to English");
    check(u(G::message("GalaxyNameShort_ModTestGalaxy", "en")) == u"Blockout Test Galaxy", "the short name defaults to the title");
    check(u(G::message("ScenarioName_ModTestGalaxy1", "en")) == u"The Checkered Box", "mission 1");
    check(u(G::message("ScenarioName_ModTestGalaxy2", "en")) == u"Second", "mission 2 as a plain string");
    check(G::message("ScenarioName_ModTestGalaxy3", "en") == nullptr, "no mission 3");
    check(G::message("GalaxyName_EggStarGalaxy", "en") == nullptr, "disc galaxies are not answered");
    check(G::message("ScenarioName_ModTestGalaxyX", "en") == nullptr, "a malformed mission id is not answered");
    const char16_t* pBefore = G::message("GalaxyName_ModTestGalaxy", "en");
    check(G::loadText(R"({"format":1,"galaxies":[{"name":"OtherGalaxy","title":"Other"}]})", "Other", &error), "a second mod loads");
    check(G::message("GalaxyName_ModTestGalaxy", "en") == pBefore, "message pointers stay valid as galaxies are added");
    check(G::musicFrom("ModTestGalaxy") != nullptr && std::string(G::musicFrom("ModTestGalaxy")) == "EggStarGalaxy", "borrowed music");
    check(G::musicFrom("OtherGalaxy") == nullptr && G::musicFrom("EggStarGalaxy") == nullptr, "no music borrowed otherwise");
    check(G::loadText(R"({"format":1,"galaxies":[{"name":"ModTestGalaxy","title":"Dup"}]})", "Late", &error) &&
              G::find("ModTestGalaxy")->mod == "GalaxyTest",
          "a name already registered by an earlier mod is kept");
    G::reject("OtherGalaxy", "test");
    check(G::find("OtherGalaxy") == nullptr && G::message("GalaxyName_OtherGalaxy", "en") == nullptr && G::all().size() == 1,
          "a rejected galaxy is gone");
}

static void testRefused() {
    G::clear();
    std::string error;
    check(!G::loadText("{", "M", &error) && !error.empty(), "broken JSON is refused");
    check(!G::loadText(R"({"format":2,"galaxies":[]})", "M", &error), "an unknown format is refused");
    check(!G::loadText(R"({"format":1,"galaxies":[]})", "M", &error), "an empty list is refused");
    check(!G::loadText(R"({"format":1,"galaxies":[{"name":"Bad Name","title":"x"}]})", "M", &error), "a name with a space is refused");
    check(!G::loadText(R"({"format":1,"galaxies":[{"name":"AVeryLongGalaxyNameThatDoesNotFitTheBuffers1234","title":"x"}]})", "M", &error),
          "a name longer than 40 is refused");
    check(!G::loadText(R"({"format":1,"galaxies":[{"name":"NoTitle"}]})", "M", &error), "a galaxy without a title is refused");
    check(!G::loadText(R"({"format":1,"galaxies":[{"name":"A","title":"a"},{"name":"B"}]})", "M", &error) && G::find("A") == nullptr,
          "one bad galaxy refuses the whole file");
    check(G::all().empty(), "nothing registered from refused files");
}

static void testSaveKey() {
    // The game's JGadget hash truncated to 16 bits (GameDataGalaxyStorage's record key).
    std::uint32_t h = 0;
    for (const char* p = "EggStarGalaxy"; *p; p++) {
        h = static_cast< std::uint32_t >(*p) + h * 31;
    }
    check(G::saveKey("EggStarGalaxy") == static_cast< std::uint16_t >(h), "save key = 16-bit JGadget hash");
    check(G::saveKey("ModTestGalaxy") != G::saveKey("EggStarGalaxy"), "distinct names, distinct keys here");
}

static void testLoadFromFolders() {
    G::clear();
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "petari_mod_galaxy_tests";
    fs::remove_all(root);
    fs::create_directories(root / "Good");
    fs::create_directories(root / "Broken");
    fs::create_directories(root / "Empty");
    std::ofstream(root / "Good" / "galaxy.json") << R"({"format":1,"galaxies":[{"name":"FolderGalaxy","title":"Folder"}]})";
    std::ofstream(root / "Broken" / "galaxy.json") << "not json";
    std::vector< std::string > log;
    G::load({root / "Good", root / "Broken", root / "Empty"}, &log);
    check(G::find("FolderGalaxy") != nullptr && G::find("FolderGalaxy")->mod == "Good", "loaded from <mod>/galaxy.json");
    bool refused = false, registered = false;
    for (const std::string& line : log) {
        refused = refused || line.find("Broken/galaxy.json refused") != std::string::npos;
        registered = registered || line.find("FolderGalaxy from mod Good") != std::string::npos;
    }
    check(refused && registered && log.size() == 2, "the log names what was registered and refused; a mod without galaxy.json is silent");
    fs::remove_all(root);
}

int main() {
    testParse();
    testRefused();
    testSaveKey();
    testLoadFromFolders();
    if (sFailures != 0) {
        std::fprintf(stderr, "%d mod galaxy check(s) failed\n", sFailures);
        return 1;
    }
    std::puts("mod galaxy registry tests passed");
    return 0;
}
