// Tests for the shared load and save (native/editor/runtime/model_io.hpp), without the game, on
// Good Egg from the extracted disc: load through ModelIO, an edit, a patch save and a Full save
// into a mod folder; the patch builds the archive the Full save wrote; each form removes the
// other's files; a folder the editor didn't make is refused.

#include "model_io.hpp"
#include <petari/editor/catalog.hpp>
#include <petari/editor/patch.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <unistd.h>

namespace E = PetariEditor;
namespace F = PetariEditor::Formats;
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

std::optional< F::Bytes > readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return F::Bytes(std::istreambuf_iterator< char >(in), {});
}
}  // namespace

int main() {
    const fs::path files = PETARI_TEST_DISC_FILES;
    if (!fs::is_directory(files / "StageData")) {
        std::printf("editor save: no extracted disc; skipped\n");
        return 0;
    }
    const E::FileSource disc = [&](const std::string& path) { return readFile(files / path.substr(1)); };
    const fs::path root = fs::temp_directory_path() / ("petari-save-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    std::string error, catalogError;
    bool registered = false;
    std::unique_ptr< E::EditModel > m = E::ModelIO::load("EggStarGalaxy", 1, (root / "user").string(), [&] { registered = true; }, &error,
                                                       &catalogError, disc, disc);
    check(m != nullptr && registered, "loads through ModelIO: " + error);
    check(std::string(m->missionName(1)) == "Dino Piranha" && std::string(m->missionName(2)) == "A Snack of Cosmic Proportions",
          std::string("English mission names from the disc's messages: ") + m->missionName(1));
    check(E::Catalog::find("EggStarGalaxyPlanet") != nullptr || E::Catalog::classes().size() > 1183, "the disc's planets are classes");

    int soramame = -1;
    for (int z = 0; z < m->zoneCount(); z++)
        if (std::string(m->zoneName(z)) == "SoramameZone") soramame = z;
    E::RowId takobo;
    for (int t = 0; t < m->tableCount(soramame) && !takobo.valid(); t++) {
        if (std::string(m->tablePath(soramame, t)) != "jmp/placement/layera/objinfo") continue;
        m->forEachRow(soramame, t, [&](E::RowId id) {
            if (!takobo.valid() && std::string(m->objectName(id)) == "Takobo") takobo = id;
        });
    }
    const float y = m->transform(takobo).pos.y;
    check(m->apply(E::Op{"Move", {E::SetCell{takobo, E::columnHash("pos_y"), E::Cell{E::CellType::Float, 0, y + 300.0f, {}}}}, {}}, &error),
          error);

    const fs::path mod = root / "user/mods/Test Mod";
    std::string summary;
    check(E::ModelIO::saveToDirectory(*m, mod, false, &summary, &error, disc), "patch save: " + error);
    check(summary == "1 edit as a patch" && fs::is_regular_file(mod / "edits/EggStarGalaxy.json") && fs::is_regular_file(mod / "mod.txt"),
          "a patch and mod.txt (" + summary + ")");
    check(E::ModelIO::saveToDirectory(*m, mod, true, &summary, &error, disc), "full save: " + error);
    const fs::path archive = mod / "files/StageData/SoramameZone.arc";
    check(summary == "1 archive" && fs::is_regular_file(archive) && !fs::exists(mod / "edits/EggStarGalaxy.json"),
          "Full archives replace the patch (" + summary + ")");

    // The patch (made again) builds the archive the Full save wrote.
    const std::string patch = m->patch().json;
    std::vector< std::string > log;
    auto built = E::Patch::build({{"Test Mod", patch}}, [&](const std::string& path) { return readFile(files / path); }, &log);
    std::optional< F::Bytes > full = readFile(archive);
    std::optional< F::Bytes > plain = full ? F::yaz0Decode(*full) : std::nullopt;
    check(built.size() == 1 && plain && built[0].bytes == *plain, "the patch builds exactly the Full save's archive");
    check(E::ModelIO::saveToDirectory(*m, mod, false, &summary, &error, disc) && !fs::exists(archive), "a patch save removes the Full archive");

    // The editor reads a patch-built archive rebuilt from the row edits, not the loader's copy
    // (which has the asset builders' output: terrain's baked brushes; EP-006).
    const F::Bytes baked = *readFile(files / "StageData/EggStarGalaxy.arc");  // anything but the patch's result
    const E::FileSource loaderCopy = [&](const std::string& path) -> std::optional< F::Bytes > {
        return path == "/StageData/SoramameZone.arc" ? std::optional< F::Bytes >(baked) : disc(path);
    };
    const E::FileSource editing = E::ModelIO::editingSource(loaderCopy, disc, {{"Test Mod", patch}},
                                                           [](const std::string& path) { return path == "/StageData/SoramameZone.arc"; });
    std::optional< F::Bytes > forEditor = editing("/StageData/SoramameZone.arc");
    check(forEditor && *forEditor == built[0].bytes, "a patch-built archive is read rebuilt from the row edits");
    check(editing("/StageData/EggStarGalaxy.arc") == disc("/StageData/EggStarGalaxy.arc"), "other archives as mounted");
    const E::FileSource notBuilt = E::ModelIO::editingSource(loaderCopy, disc, {{"Test Mod", patch}}, [](const std::string&) { return false; });
    check(notBuilt("/StageData/SoramameZone.arc") == baked, "an archive the loader didn't build is read as mounted");

    fs::create_directories(root / "user/mods/Other");
    std::ofstream(root / "user/mods/Other/mod.txt") << "name=Other\n";
    check(!E::ModelIO::saveToDirectory(*m, root / "user/mods/Other", false, &summary, &error, disc) &&
              error.find("not made by the editor") != std::string::npos,
          "a folder the editor didn't make is refused");
    check(E::ModelIO::validModName("Coin Ring 2") && !E::ModelIO::validModName("../x") && !E::ModelIO::validModName(""), "mod names");
    fs::remove_all(root);
    std::printf("editor save: %d checks passed\n", checks);
    return 0;
}
