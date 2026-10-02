// --stage name resolution and mission range checks (native/home_menu/launch_stage.cpp).
#include "petari/launch_stage.hpp"
#include <petari/platform/mod_galaxy.hpp>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace LS = PetariNative::App::LaunchStage;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    std::string stage, error;
    check(LS::resolve("good-egg", 1, &stage, &error) && stage == "EggStarGalaxy", "alias");
    check(LS::resolve("Good Egg Galaxy", 4, &stage, &error) && stage == "EggStarGalaxy", "English name, comet mission");
    check(LS::resolve("eggstargalaxy", 6, &stage, &error) && stage == "EggStarGalaxy", "internal name, any case, hidden star");
    check(LS::resolve("bowser-jrs-robot-reactor", 1, &stage, &error) && stage == "TriLegLv1Galaxy", "punctuated name");
    check(LS::resolve("Bowser Jr.'s Robot Reactor", 1, &stage, &error) && stage == "TriLegLv1Galaxy", "apostrophes ignored");
    check(LS::resolve("battlerock", 7, &stage, &error) && stage == "BattleShipGalaxy", "seven-mission galaxy");
    check(!LS::resolve("good-egg", 7, &stage, &error) && error.find("missions 1-6") != std::string::npos, "mission past the end");
    check(!LS::resolve("good-egg", 0, &stage, &error), "mission zero");
    check(!LS::resolve("nowhere", 1, &stage, &error) && error.find("--stage list") != std::string::npos, "unknown galaxy");
    check(!LS::resolve("", 1, &stage, &error), "empty name");
    check(LS::find("AstroDome") == nullptr && LS::find("FileSelect") == nullptr, "hubs and menus are not destinations");

    int count = 0;
    const LS::Galaxy* all = LS::galaxies(&count);
    std::set<std::string> keys;
    int perDome[7] = {};
    for (int i = 0; i < count; ++i) {
        check(LS::find(all[i].alias) == &all[i], "every alias resolves to itself");
        check(LS::find(all[i].stage) == &all[i], "every internal name resolves to itself");
        check(keys.insert(all[i].alias).second, "aliases are unique");
        check(all[i].missions >= 1 && all[i].dome >= 0 && all[i].dome <= 6, "sane mission count and dome");
        ++perDome[all[i].dome];
    }
    check(count == 42, "42 galaxies listed");
    for (int dome = 1; dome <= 6; ++dome) check(perDome[dome] >= 4, "every dome has its galaxies");
    check(std::strcmp(LS::domeName(1), "Terrace") == 0 && std::strcmp(LS::domeName(0), "Elsewhere") == 0, "dome names");

    std::FILE* out = std::tmpfile();
    LS::printList(out);
    std::rewind(out);
    char buffer[256];
    std::string text;
    while (std::fgets(buffer, sizeof(buffer), out)) text += buffer;
    std::fclose(out);
    check(text.find("good-egg") != std::string::npos && text.find("[1 2 3 4C 5C 6H]") != std::string::npos,
          "list shows aliases and comet/hidden missions");
    check(text.find("Terrace") < text.find("Garden") && text.find("Garden") < text.find("Elsewhere"), "list is by dome");

    // Mod galaxies (the registry) join the list in their own section, after the disc's.
    std::string error2;
    check(PetariNative::Platform::ModGalaxy::loadText(
              R"({"format":1,"galaxies":[{"name":"ModTestGalaxy","title":"Blockout Test Galaxy","missions":["One","Two"]}]})", "GalaxyTest",
              &error2),
          "registry loads");
    const LS::Galaxy* before = &all[0];
    all = LS::galaxies(&count);
    check(count == 43 && std::strcmp(all[42].stage, "ModTestGalaxy") == 0 && all[42].dome == 7 && all[42].missions == 2 &&
              std::strcmp(all[42].name, "Blockout Test Galaxy") == 0,
          "a registered mod galaxy is listed last, in the mod galaxies section");
    check(std::strcmp(LS::domeName(7), "Mod galaxies") == 0, "mod galaxies section name");
    check(LS::resolve("ModTestGalaxy", 2, &stage, &error) && stage == "ModTestGalaxy", "--stage takes a mod galaxy");
    check(LS::resolve("blockout test", 1, &stage, &error) && stage == "ModTestGalaxy", "--stage takes its English name");
    check(!LS::resolve("ModTestGalaxy", 3, &stage, &error), "mod galaxy mission range");
    (void)before;
    out = std::tmpfile();
    LS::printList(out);
    std::rewind(out);
    text.clear();
    while (std::fgets(buffer, sizeof(buffer), out)) text += buffer;
    std::fclose(out);
    check(text.find("Mod galaxies") > text.find("Elsewhere") && text.find("ModTestGalaxy") > text.find("Mod galaxies"), "list ends with mod galaxies");

    if (failures) return 1;
    std::puts("launch stage: aliases, names, mission ranges and list pass");
    return 0;
}
