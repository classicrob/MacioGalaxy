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
    const auto skies = G::skyNames(list);
    check(skies.size() >= 5 && std::find(skies.begin(), skies.end(), "PhantomSky") != skies.end(), "the disc's skies");
    // The skies in a stage's objects (any layer).
    auto skiesIn = [](const F::Archive& stage) {
        std::string found;
        for (const std::string& path : stage.files()) {
            if (path.size() < 8 || path.compare(path.size() - 8, 8, "/objinfo") != 0) {
                continue;
            }
            const auto objects = F::Table::parse(*stage.file(path));
            const int name = objects ? objects->column(F::jmapHash("name")) : -1;
            for (int r = 0; name >= 0 && r < objects->rowCount(); r++) {
                const std::string object = objects->get(r, name).s;
                if (object.find("Sky") != std::string::npos || object.rfind("VR", 0) == 0) {
                    found += (found.empty() ? "" : ",") + object;
                }
            }
        }
        return found;
    };

    struct Case {
        const char* name;
        G::Template from;
        int missions;
        const char* sky;      // asked for ("" = the default)
        const char* skyWant;  // in the stage after ("" = whatever the default gives, but one)
    };
    for (const Case& c : {Case{"Empty Sky", G::Template::Empty, 1, "", ""}, Case{"Flower Hop", G::Template::OnePlanet, 3, "", ""},
                          Case{"Box World", G::Template::Brushes, 2, "PhantomSky", "PhantomSky"},
                          Case{"Egg Again", G::Template::Copy, 0, "PhantomSky", "PhantomSky"}}) {
        G::NewGalaxySpec spec;
        spec.name = c.name;
        spec.from = c.from;
        spec.planet = "FlowerPlanet";
        spec.copyOf = "EggStarGalaxy";
        spec.sky = c.sky;
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
        const std::string sky = skiesIn(*stage);
        check(*c.skyWant != '\0' ? sky == c.skyWant : !sky.empty() && sky.find(',') == std::string::npos, where + "one sky: " + sky);
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
        // The Missions panel's mission count: grown to 4 (new rows with their star and layer), then 1.
        const F::Bytes& scenarioArc = out.files.at("StageData/" + g + "/" + g + "Scenario.arc");
        const F::Bytes grown = G::resizeScenario(scenarioArc, g, 4, &error);
        const auto grownArchive = grown.empty() ? std::nullopt : F::Archive::parse(grown);
        const auto grownTable = grownArchive ? F::Table::parse(*grownArchive->file("scenariodata.bcsv")) : std::nullopt;
        check(grownTable && grownTable->rowCount() == 4, where + "4 missions after the resize: " + error);
        if (grownTable && scenarios && c.from != G::Template::Copy) {  // (a copy keeps the source's rows)
            const int no = grownTable->column(F::jmapHash("ScenarioNo")), star = grownTable->column(F::jmapHash("PowerStarId")),
                      layer = grownTable->column(F::jmapHash(g.c_str()));
            check(grownTable->get(3, no).i == 4 && grownTable->get(3, star).i == 4 && grownTable->get(3, layer).i == 8,
                  where + "mission 4: star 4, Layer D");
            check(grownTable->get(0, layer).i == scenarios->get(0, scenarios->column(F::jmapHash(g.c_str()))).i, where + "mission 1 kept as it was");
            check(grownArchive->file("zonelist.bcsv") != nullptr, where + "the zone list kept");
        }
        const F::Bytes shrunk = G::resizeScenario(grown, g, 1, &error);
        const auto shrunkArchive = shrunk.empty() ? std::nullopt : F::Archive::parse(shrunk);
        const auto shrunkTable = shrunkArchive ? F::Table::parse(*shrunkArchive->file("scenariodata.bcsv")) : std::nullopt;
        check(shrunkTable && shrunkTable->rowCount() == 1, where + "1 mission after the second resize");
        check(G::resizeScenario(scenarioArc, g, 9, &error).empty() && !error.empty(), where + "9 missions are refused");
    }
    // galaxyJson as the Missions panel writes it, through the registry's update.
    PetariNative::Platform::ModGalaxy::clear();
    std::string error;
    PetariNative::Platform::ModGalaxy::loadText(G::galaxyJson("PipeDreamGalaxy", "Pipe Dream", {"One"}, "EggStarGalaxy"), "PipeDreamGalaxy", &error);
    check(PetariNative::Platform::ModGalaxy::update(G::galaxyJson("PipeDreamGalaxy", "Pipe \"Dreams\"", {"One", "Two"}, "FlowerGalaxy"), "PipeDreamGalaxy", &error),
          "the edited galaxy.json updates the registry: " + error);
    const auto* pGalaxy = PetariNative::Platform::ModGalaxy::find("PipeDreamGalaxy");
    check(pGalaxy != nullptr && pGalaxy->missions.size() == 2 && pGalaxy->music == "FlowerGalaxy" && pGalaxy->title.at("en") == u"Pipe \"Dreams\"",
          "renamed, a second mission, other music");
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
