// Tests for sharing mods (native/editor/include/petari/editor/share.hpp): the zip writer and reader
// (and that the system's unzip agrees), Export with the sheet's fields, the Install sheet's
// preview, and installing (replace, refusing paths that escape, a zip that isn't one mod).

#include <petari/editor/share.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace S = PetariEditor::Share;
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

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator< char >(in), {});
}

std::vector< std::uint8_t > bytes(const std::string& s) {
    return std::vector< std::uint8_t >(s.begin(), s.end());
}
}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("petari-share-test-" + std::to_string(::getpid()));
    fs::remove_all(root);

    // The zip format.
    std::string big(200000, 'a');
    for (size_t i = 0; i < big.size(); i += 7) big[i] = static_cast< char >('a' + i % 26);
    const std::vector< S::ZipEntry > entries = {{"Mod/empty.txt", {}}, {"Mod/big.bin", bytes(big)}, {"Mod/sub/x.json", bytes("{\"a\": 1}")}};
    const std::vector< std::uint8_t > zip = S::writeZip(entries);
    check(zip.size() < big.size() / 2, "compressible data is deflated");
    std::string error;
    std::optional< std::vector< S::ZipEntry > > back = S::readZip(zip, &error);
    check(back && back->size() == 3, "read back: " + error);
    for (size_t i = 0; back && i < 3; i++) check((*back)[i].path == entries[i].path && (*back)[i].data == entries[i].data, "entry " + std::to_string(i));
    std::vector< std::uint8_t > damaged = zip;
    damaged[120] ^= 0xFF;  // inside big.bin's deflated data (its local header ends at 84)
    check(!S::readZip(damaged, &error), "a damaged zip is refused (" + error + ")");
    check(!S::readZip(bytes("not a zip at all, just some text"), &error), "not a zip");
    fs::create_directories(root);
    {
        std::ofstream(root / "plain.zip", std::ios::binary).write(reinterpret_cast< const char* >(zip.data()), zip.size());
    }
    if (fs::exists("/usr/bin/unzip")) {
        const std::string command = "/usr/bin/unzip -tq '" + (root / "plain.zip").string() + "' > /dev/null 2>&1";
        check(std::system(command.c_str()) == 0, "the system's unzip accepts our zips");
    }

    // Export: the sheet's fields go into mod.txt (other lines kept), the preview beside it.
    const fs::path mods = root / "mods";
    write(mods / "Coin Ring/mod.txt", "name=Coin Ring\n# a comment\neditor=1\npriority=2\n");
    write(mods / "Coin Ring/edits/EggStarGalaxy.json", "{\"format\": 1, \"archives\": {\"StageData/SoramameZone.arc\": []}}");
    write(mods / "Coin Ring/galaxy.json", "{\"galaxies\": [{\"name\": \"PipeDreamGalaxy\"}]}");
    write(mods / "Coin Ring/.DS_Store", "junk");
    S::Info info;
    info.author = "Ada";
    info.version = "1.1";
    info.description = "A ring of coins\naround the start";
    info.needs = {"OdysseyMovement", "Cappy"};
    info.previewPng = bytes("\x89PNG fake");
    check(S::exportZip(mods / "Coin Ring", info, root / "out/Coin Ring-1.1.zip", &error), "export: " + error);
    const std::string modText = read(mods / "Coin Ring/mod.txt");
    check(modText == "name=Coin Ring\n# a comment\neditor=1\npriority=2\nauthor=Ada\nversion=1.1\ndescription=A ring of coins around the start\n"
                     "needs=OdysseyMovement,Cappy\n",
          "mod.txt: fields set, other lines kept (" + modText + ")");

    // The Install sheet.
    std::optional< S::Preview > preview = S::inspectZip(root / "out/Coin Ring-1.1.zip", &error);
    check(preview.has_value(), "inspect: " + error);
    check(preview->folder == "Coin Ring" && preview->title == "Coin Ring" && preview->author == "Ada" && preview->version == "1.1" &&
              preview->needs == std::vector< std::string >{"OdysseyMovement", "Cappy"} && preview->editorMod && !preview->fullArchives,
          "the preview's fields");
    check((preview->galaxies == std::vector< std::string >{"EggStarGalaxy", "PipeDreamGalaxy"}) &&
              preview->archives == std::vector< std::string >{"StageData/SoramameZone.arc"} && preview->previewPng == info.previewPng,
          "what it changes, and the screenshot");
    std::optional< std::vector< S::ZipEntry > > zipped = S::readZip(bytes(read(root / "out/Coin Ring-1.1.zip")), &error);
    bool clutter = false;
    for (const S::ZipEntry& e : *zipped) clutter |= e.path.find(".DS_Store") != std::string::npos;
    check(!clutter, "host clutter is left out");
    check(S::describeFolder(mods / "Coin Ring").author == "Ada", "a folder is described the same way");
    check(preview->newGalaxies == std::vector< std::string >{"PipeDreamGalaxy"} && !preview->newGalaxyFilesOnly,
          "a new galaxy is named (it plays only in Petari)");
    // A new galaxy's own archives are not a replaced disc file.
    write(mods / "Pipe/galaxy.json", "{\"galaxies\": [{\"name\": \"PipeGalaxy\"}]}");
    write(mods / "Pipe/files/StageData/PipeGalaxy.arc", "stage");
    write(mods / "Pipe/files/StageData/PipeGalaxy/PipeGalaxyScenario.arc", "scenario");
    const S::Preview pipe = S::describeFolder(mods / "Pipe");
    check(pipe.fullArchives && pipe.newGalaxyFilesOnly, "a new galaxy's own archives only");
    write(mods / "Pipe/files/StageData/EggStarGalaxy.arc", "replaced");
    check(!S::describeFolder(mods / "Pipe").newGalaxyFilesOnly, "plus a replaced disc archive: game data again");

    // Install, again (refused), replace.
    const fs::path other = root / "other-mods";
    std::string folder;
    check(S::installZip(root / "out/Coin Ring-1.1.zip", other, false, &folder, &error) && folder == "Coin Ring", "install: " + error);
    check(read(other / "Coin Ring/edits/EggStarGalaxy.json") == read(mods / "Coin Ring/edits/EggStarGalaxy.json"), "the files arrive");
    check(!S::installZip(root / "out/Coin Ring-1.1.zip", other, false, &folder, &error) && error.find("already installed") != std::string::npos,
          "installing twice asks first");
    write(other / "Coin Ring/stale.txt", "old");
    check(S::installZip(root / "out/Coin Ring-1.1.zip", other, true, &folder, &error) && !fs::exists(other / "Coin Ring/stale.txt"),
          "replace swaps the whole folder");

    // Zips that aren't one safe mod folder.
    auto zipFile = [&](const char* pName, const std::vector< S::ZipEntry >& list) {
        const std::vector< std::uint8_t > z = S::writeZip(list);
        std::ofstream(root / pName, std::ios::binary).write(reinterpret_cast< const char* >(z.data()), z.size());
        return root / pName;
    };
    const fs::path evil = zipFile("evil.zip", {{"Evil/mod.txt", bytes("name=Evil")}, {"Evil/../../escaped.txt", bytes("x")}});
    check(!S::installZip(evil, other, false, &folder, &error) && !fs::exists(root / "escaped.txt") && !fs::exists(other / "Evil"),
          "a path escaping the folder is refused, nothing written (" + error + ")");
    const fs::path two = zipFile("two.zip", {{"A/mod.txt", bytes("x")}, {"B/mod.txt", bytes("y")}});
    check(!S::inspectZip(two, &error) && error.find("one mod folder") != std::string::npos, "two top folders are refused");
    const fs::path empty = zipFile("empty.zip", {{"Nothing/readme.txt", bytes("hi")}});
    check(!S::inspectZip(empty, &error), "a zip without a mod is refused");

    fs::remove_all(root);
    std::printf("editor share: %d checks passed\n", checks);
    return 0;
}
