// New galaxies from templates (petari/editor/galaxy/new_galaxy.hpp): names, the disc's lists, and
// each template's files, read back with the formats library and the mod galaxy registry.
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/galaxy/new_galaxy.hpp>
#include <petari/platform/mod_galaxy.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace F = PetariEditor::Formats;
namespace G = PetariEditor::Galaxy;
namespace fs = std::filesystem;

static int sFailures = 0;

static void check(bool condition, const std::string& text) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", text.c_str());
        ++sFailures;
    }
}

static void testNames() {
    check(G::internalNameFor("Pipe Dream") == "PipeDreamGalaxy", "Pipe Dream -> PipeDreamGalaxy");
    check(G::internalNameFor("windward hop-scotch galaxy") == "WindwardHopScotchGalaxy", "words capitalised; Galaxy kept once");
    check(G::internalNameFor("3 Moons") == "Mod3MoonsGalaxy", "a leading digit gets a prefix");
    const G::DiscLister none = [](const std::string&) { return std::vector< std::string >{"EggStarGalaxy.arc", "EggStarGalaxy"}; };
    check(!G::checkInternalName("EggStarGalaxy", none, {}).empty(), "a disc galaxy's name is refused");
    check(!G::checkInternalName("PipeDreamGalaxy", none, {"pipedreamgalaxy"}).empty(), "a registered name is refused");
    check(!G::checkInternalName("Pipe Dream", none, {}).empty(), "spaces are refused");
    check(G::checkInternalName("PipeDreamGalaxy", none, {}).empty(), "a free name passes");
}

#ifdef PETARI_TEST_DISC_FILES
static void testDisc(const fs::path& disc) {
    const G::DiscReader read = [&](const std::string& path) -> std::optional< F::Bytes > {
        std::ifstream file(disc / path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        return F::Bytes((std::istreambuf_iterator< char >(file)), std::istreambuf_iterator< char >());
    };
    const G::DiscLister list = [&](const std::string& directory) {
        std::vector< std::string > names;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(disc / directory, ec)) {
            names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    if (!read("StageData/EggStarGalaxy.arc")) {
        std::printf("new galaxy: no disc, skipped\n");
        return;
    }
    const auto galaxies = G::discGalaxies(list);
    check(std::find(galaxies.begin(), galaxies.end(), "EggStarGalaxy") != galaxies.end() && galaxies.size() > 40, "the disc's galaxies");
    const auto planets = G::planetNames(read);
    check(std::find(planets.begin(), planets.end(), "FlowerPlanet") != planets.end() && planets.size() > 100, "the disc's planets");

    struct Case {
        const char* name;
        G::Template from;
        int missions;
    };
    for (const Case& c : {Case{"Empty Sky", G::Template::Empty, 1}, Case{"Flower Hop", G::Template::OnePlanet, 3},
                          Case{"Box World", G::Template::Brushes, 2}, Case{"Egg Again", G::Template::Copy, 0}}) {
        G::NewGalaxySpec spec;
        spec.name = c.name;
        spec.from = c.from;
        spec.planet = "FlowerPlanet";
        spec.copyOf = "EggStarGalaxy";
        for (int m = 1; m <= c.missions; m++) {
            spec.missions.push_back(std::string("Mission \"") + std::to_string(m) + "\"");
        }
        const G::NewGalaxyFiles out = G::buildNewGalaxy(spec, read, list);
        const std::string where = std::string(c.name) + ": ";
        check(out.error.empty(), where + out.error);
        if (!out.error.empty()) {
            continue;
        }
        for (const std::string& line : out.log) {
            std::printf("  %s%s\n", where.c_str(), line.c_str());
        }
        const std::string g = out.internalName;
        const auto stage = out.files.count("StageData/" + g + ".arc") ? F::Archive::parse(out.files.at("StageData/" + g + ".arc")) : std::nullopt;
        const auto scenario = out.files.count("StageData/" + g + "/" + g + "Scenario.arc")
                                  ? F::Archive::parse(out.files.at("StageData/" + g + "/" + g + "Scenario.arc"))
                                  : std::nullopt;
        check(stage && scenario, where + "a stage and a Scenario archive");
        if (!stage || !scenario) {
            continue;
        }
        const auto scenarios = F::Table::parse(*scenario->file("scenariodata.bcsv"));
        const auto zones = F::Table::parse(*scenario->file("zonelist.bcsv"));
        check(scenarios && zones && zones->get(0, zones->column(F::jmapHash("ZoneName"))).s == g, where + "zone 0 is the galaxy itself");
        check(scenarios && scenarios->column(F::jmapHash(g.c_str())) >= 0, where + "ScenarioData has the galaxy's layer column");
        if (c.from != G::Template::Copy) {
            check(scenarios && scenarios->rowCount() == std::max(c.missions, 1), where + "one scenario row per mission");
            const auto start = F::Table::parse(*stage->file("jmp/start/common/startinfo"));
            check(start && start->rowCount() == 1, where + "one start");
        }
        if (c.from == G::Template::OnePlanet) {
            const auto gravity = F::Table::parse(*stage->file("jmp/placement/common/planetobjinfo"));
            check(gravity && gravity->rowCount() == 1, where + "the planet's gravity");
            check(stage->has("jmp/placement/layerc/objinfo"), where + "mission 3's star on Layer C");
        }
        // The registry takes the entry (names, missions, music).
        PetariNative::Platform::ModGalaxy::clear();
        std::string error;
        check(PetariNative::Platform::ModGalaxy::loadText(out.galaxyJson, "Test", &error), where + "galaxy.json loads: " + error);
        const auto* pGalaxy = PetariNative::Platform::ModGalaxy::find(g.c_str());
        check(pGalaxy != nullptr, where + "the registry knows it");
    }
    PetariNative::Platform::ModGalaxy::clear();
}
#endif

int main() {
    testNames();
#ifdef PETARI_TEST_DISC_FILES
    testDisc(PETARI_TEST_DISC_FILES);
#endif
    if (sFailures != 0) {
        std::fprintf(stderr, "%d new galaxy check(s) failed\n", sFailures);
        return 1;
    }
    std::puts("new galaxy tests passed");
    return 0;
}
