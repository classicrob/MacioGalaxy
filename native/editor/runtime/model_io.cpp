// Loading for editing and saving as a mod, shared by the Runtime and headless tools (model_io.hpp).
#include "model_io.hpp"

#include <petari/editor/areas.hpp>
#include <petari/editor/catalog.hpp>
#include <petari/editor/check_providers.hpp>
#include <petari/editor/paths.hpp>
#include <petari/editor/terrain/bake.hpp>
#include <petari/platform/dvd.hpp>
#include <revolution/dvd.h>

#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <vector>

namespace PetariEditor::ModelIO {
namespace fs = std::filesystem;

namespace {
    // The paths module's checks (switch nets, links), for every session in-game or headless.
    void pathChecks(const ModelView& model, std::vector< Check >& out) {
        std::vector< Check > checks = Paths::computeChecks(model);
        out.insert(out.end(), checks.begin(), checks.end());
    }

    // Written next to the target, then renamed over it, so a crash never leaves half a file.
    bool writeAtomically(const fs::path& path, const Formats::Bytes& bytes, std::string* pError) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const fs::path temp = path.string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast< const char* >(bytes.data()), static_cast< std::streamsize >(bytes.size()));
            if (!out) {
                *pError = "cannot write " + temp.string();
                return false;
            }
        }
        fs::rename(temp, path, ec);
        if (ec) {
            *pError = "cannot replace " + path.string() + ": " + ec.message();
            fs::remove(temp, ec);
            return false;
        }
        return true;
    }

    bool isEditorMod(const fs::path& root) {
        std::ifstream in(root / "mod.txt");
        std::string line;
        while (std::getline(in, line)) {
            if (line == "editor=1") {
                return true;
            }
        }
        return false;
    }
}  // namespace

std::optional< Formats::Bytes > readMounted(const std::string& path) {
    const s32 entry = DVDConvertPathToEntrynum(path.c_str());
    if (entry < 0) {
        return std::nullopt;
    }
    std::ifstream in(PetariNative::Platform::DVD::hostPathForEntry(entry), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
}

std::optional< Formats::Bytes > readUnmodded(const std::string& path) {
    const fs::path files = PetariNative::Platform::DVD::mountInfo().filesDirectory;
    std::ifstream in(files / path.substr(1), std::ios::binary);
    if (files.empty() || !in) {
        return std::nullopt;
    }
    return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
}

std::unique_ptr< EditModel > load(const std::string& galaxy, int scenario, const std::string& userDirectory,
                                  const std::function< void() >& registerClasses, std::string* pError, std::string* pCatalogError,
                                  const FileSource& mounted, const FileSource& unmodded) {
    if (!Catalog::loadWithUser(userDirectory, pCatalogError)) {
        // Reported by the caller; the galaxy still loads (checks then know fewer classes).
    }
    if (registerClasses) {
        registerClasses();
    }
    Areas::registerCatalog();
    registerCheckProvider(&Areas::computeChecks);
    registerCheckProvider(&pathChecks);  // before the first computeChecks: part of the baseline
    // Planet classes: the names PlanetMapDataTable lists on this disc.
    if (std::optional< Formats::Bytes > raw = mounted("/ObjectData/PlanetMapDataTable.arc")) {
        if (std::optional< Formats::Archive > archive = Formats::Archive::parse(*raw)) {
            for (const std::string& file : archive->files()) {
                std::optional< Formats::Table > table = Formats::Table::parse(*archive->file(file));
                const int column = table ? table->column(Formats::jmapHash("PlanetName")) : -1;
                std::vector< std::string > names;
                for (int r = 0; column >= 0 && r < table->rowCount(); r++) {
                    names.push_back(table->get(r, column).s);
                }
                Catalog::addPlanets(names);
            }
        }
    }
    return EditModel::load(galaxy, scenario, mounted, pError, unmodded);
}

bool validModName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name == "." || name == "..") {
        return false;
    }
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
                        c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool saveToDirectory(const EditModel& model, const fs::path& root, bool fullArchives, std::string* pSummary, std::string* pError,
                     const FileSource& mounted) {
    std::error_code ec;
    if (fs::exists(root, ec) && !isEditorMod(root)) {
        *pError = "mods/" + root.filename().string() + " exists and was not made by the editor";
        return false;
    }

    const fs::path patchFile = root / "edits" / (std::string(model.galaxy()) + ".json");
    int written = 0;
    std::set< std::string > changed;
    std::string summary;
    if (!fullArchives) {
        // The patch, plus the zones it can't express (a mod's own zones) as whole archives. This
        // galaxy's other archives leave the mod: the patch holds every difference from the disc.
        const EditModel::PatchOut patch = model.patch();
        if (patch.json.empty()) {
            fs::remove(patchFile, ec);
        } else if (!writeAtomically(patchFile, Formats::Bytes(patch.json.begin(), patch.json.end()), pError)) {
            return false;
        }
        for (auto& [discPath, plain] : patch.wholeArchives) {
            if (!writeAtomically(root / "files" / discPath.substr(1), Formats::yaz0Encode(plain), pError)) {
                return false;
            }
            changed.insert(discPath);
        }
        for (int zone = 0; zone < model.zoneCount(); zone++) {
            const std::string discPath = std::string("/StageData/") + model.zoneName(zone) + ".arc";
            if (!changed.count(discPath)) {
                fs::remove(root / "files" / discPath.substr(1), ec);
            }
        }
        summary = std::to_string(patch.ops) + (patch.ops == 1 ? " edit" : " edits") + " as a patch";
        if (!patch.wholeArchives.empty()) {
            summary += " and " + std::to_string(patch.wholeArchives.size()) + " new zone archive" + (patch.wholeArchives.size() == 1 ? "" : "s");
        }
    }
    // Full archives: every zone archive that differs from the disc; the others are removed from the
    // mod, so an edit reverted since the last save takes effect. Terrain brushes become generated
    // planets (petari/editor/terrain/bake.hpp), so the mod plays without the editor. (A patch
    // holds no game data: its brush rows travel as row ops, and the loader bakes them.)
    Terrain::BakeInput bake;
    if (fullArchives) {
        for (auto& [discPath, plain] : model.changedArchives()) {
            bake.stageArchives[discPath] = plain;
        }
    }
    bake.readDisc = mounted;
    const Terrain::BakeOutput baked = Terrain::bakeBrushes(bake);
    if (!baked.error.empty()) {
        *pError = "terrain: " + baked.error;
        return false;
    }
    for (auto& [discPath, plain] : bake.stageArchives) {
        auto it = baked.files.find(discPath);
        if (!writeAtomically(root / "files" / discPath.substr(1), Formats::yaz0Encode(it != baked.files.end() ? it->second : plain), pError)) {
            return false;
        }
        changed.insert(discPath);
        written++;
    }
    // The planets and their table; those of an earlier save that are gone are removed.
    std::set< fs::path > planets;
    for (auto& [discPath, bytes] : baked.files) {
        if (discPath.rfind("/ObjectData/", 0) == 0) {
            if (!writeAtomically(root / "files" / discPath.substr(1), bytes, pError)) {
                return false;
            }
            planets.insert(root / "files" / discPath.substr(1));
        }
    }
    std::vector< fs::path > stale;
    for (const auto& entry : fs::directory_iterator(root / "files" / "ObjectData", ec)) {
        const std::string name = entry.path().filename().string();
        if ((name.rfind("EditorBrush", 0) == 0 || (name == "PlanetMapDataTable.arc" && baked.brushes == 0)) && !planets.count(entry.path())) {
            stale.push_back(entry.path());
        }
    }
    for (const fs::path& path : stale) {
        fs::remove(path, ec);
    }
    if (fullArchives) {
        for (int zone = 0; zone < model.zoneCount(); zone++) {
            const std::string discPath = std::string("/StageData/") + model.zoneName(zone) + ".arc";
            if (!changed.count(discPath)) {
                fs::remove(root / "files" / discPath.substr(1), ec);
            }
        }
        fs::remove(patchFile, ec);  // the loader would build it over these
        summary = std::to_string(written) + (written == 1 ? " archive" : " archives");
        if (baked.brushes > 0) {
            summary += " and " + std::to_string(baked.brushes) + (baked.brushes == 1 ? " terrain brush" : " terrain brushes");
        }
    }
    if (!fs::exists(root / "mod.txt", ec)) {
        Formats::Bytes text;
        const std::string body = "name=" + root.filename().string() + "\ndescription=" + model.galaxy() + ", edited in the Petari level editor\neditor=1\n";
        text.assign(body.begin(), body.end());
        if (!writeAtomically(root / "mod.txt", text, pError)) {
            return false;
        }
    }
    *pSummary = summary;
    return true;
}

}  // namespace PetariEditor::ModelIO
