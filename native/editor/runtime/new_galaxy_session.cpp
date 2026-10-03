// New galaxies in the running editor (docs/dev/level-editor/new-galaxy-flow.md): the sheet's lists
// and name checks from the mounted disc, and Command::NewGalaxy: the galaxy is written as a mod
// (petari/editor/galaxy/new_galaxy.hpp), switched on, and Petari restarts straight into it in Edit.
// The game learns its galaxies once, at boot (the parser, the save storage, the disc overlay), so a
// restart is the only safe way in; the restarted game skips the title and opens the same file
// (PETARI_OPEN_*: app_main, LogoScene, FileSelector).
#include "new_galaxy_session.hpp"
#include "runtime_core.hpp"
#include "galaxy_checks.hpp"

#include <petari/editor/galaxy/new_galaxy.hpp>
#include <petari/host_allocation.hpp>
#include <petari/platform/dvd.hpp>
#include <petari/platform/mod_folder.hpp>
#include <petari/platform/mod_galaxy.hpp>
#include <revolution/dvd.h>
#include <revolution/os.h>

#include "Game/System/GameDataFunction.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace PetariEditor::Runtime {
namespace {
    namespace fs = std::filesystem;
    namespace G = PetariEditor::Galaxy;
    namespace ModGalaxy = PetariNative::Platform::ModGalaxy;

    std::optional< Formats::Bytes > readDisc(const std::string& path) {
        const std::string disc = "/" + path;
        const s32 entry = DVDConvertPathToEntrynum(disc.c_str());
        if (entry < 0) {
            return std::nullopt;
        }
        std::ifstream in(PetariNative::Platform::DVD::hostPathForEntry(entry), std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
    }

    std::vector< std::string > listDisc(const std::string& directory) {
        std::vector< std::string > names;
        DVDDir dir;
        if (!DVDOpenDir(("/" + directory).c_str(), &dir)) {
            return names;
        }
        DVDDirEntry entry;
        while (DVDReadDir(&dir, &entry)) {
            names.push_back(entry.name);
        }
        DVDCloseDir(&dir);
        return names;
    }

    const NewGalaxyChoices& choices() {
        static NewGalaxyChoices sChoices;
        static bool sDone = false;
        if (!sDone) {
            PetariNative::HostAllocationScope host;
            sChoices.planets = G::planetNames(readDisc);
            sChoices.galaxies = G::discGalaxies(listDisc);
            sDone = !sChoices.galaxies.empty();  // again later if the disc wasn't up yet
        }
        return sChoices;
    }

    std::string internalName(const std::string& name) {
        return G::internalNameFor(name);
    }

    std::string check(const std::string& name) {
        PetariNative::HostAllocationScope host;
        std::vector< std::string > taken;
        for (const ModGalaxy::Galaxy* pGalaxy : ModGalaxy::all()) {
            taken.push_back(pGalaxy->name);
        }
        if (std::string why = G::checkInternalName(name, listDisc, taken); !why.empty()) {
            return why;
        }
        // The save keys a galaxy's progress by a 16-bit hash of its name (TECH.md 5.7).
        const std::vector< std::string >& galaxies = choices().galaxies;
        for (const std::string& other : galaxies) {
            if (ModGalaxy::saveKey(other.c_str()) == ModGalaxy::saveKey(name.c_str())) {
                return name + " would share its save record with " + other + "; choose another name";
            }
        }
        for (const std::string& other : taken) {
            if (ModGalaxy::saveKey(other.c_str()) == ModGalaxy::saveKey(name.c_str())) {
                return name + " would share its save record with " + other + "; choose another name";
            }
        }
        if (galaxies.size() + taken.size() >= 64) {
            return "the game holds at most 64 galaxies";
        }
        return "";
    }

    bool writeFile(const fs::path& path, const Formats::Bytes& bytes, std::string* pError) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const fs::path temporary = path.string() + ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast< const char* >(bytes.data()), static_cast< std::streamsize >(bytes.size()));
            if (!out) {
                *pError = "cannot write " + path.string();
                return false;
            }
        }
        fs::rename(temporary, path, ec);
        if (ec) {
            *pError = "cannot write " + path.string() + ": " + ec.message();
            return false;
        }
        return true;
    }

    int gRestartIn = 0;  // seams until OSRestart (the UI shows the notice first)
}  // namespace

void registerNewGalaxyServices() {
    registerGalaxyChecks();
    Core::NewGalaxyServices services;
    services.choices = &choices;
    services.internalName = &internalName;
    services.check = &check;
    Core::setNewGalaxyServices(services);
}

bool createNewGalaxy(const NewGalaxyRequest& request, bool unsaved, std::string* pError) {
    if (unsaved && !request.discardUnsaved) {
        *pError = "this galaxy has unsaved edits: save or discard them first";
        return false;
    }
    PetariNative::HostAllocationScope host;
    G::NewGalaxySpec spec;
    spec.name = request.name;
    spec.internalName = request.internalName.empty() ? G::internalNameFor(request.name) : request.internalName;
    spec.from = static_cast< G::Template >(request.from);
    spec.planet = request.planet;
    spec.copyOf = request.copyOf;
    spec.musicFrom = request.musicFrom;
    spec.missions = request.missions;
    if (std::string why = check(spec.internalName); !why.empty()) {
        *pError = why;
        return false;
    }
    const G::NewGalaxyFiles files = G::buildNewGalaxy(spec, readDisc, listDisc);
    if (!files.error.empty()) {
        *pError = files.error;
        return false;
    }
    const fs::path user = PetariNative::Platform::ModFolder::userDirectory();
    if (user.empty()) {
        *pError = "no user folder";
        return false;
    }
    // The mod is the galaxy's own folder (mods/<InternalName>), made by the editor.
    const fs::path root = user / "mods" / files.internalName;
    std::error_code ec;
    if (fs::exists(root, ec)) {
        *pError = "mods/" + files.internalName + " already exists";
        return false;
    }
    for (const auto& [path, bytes] : files.files) {
        if (!writeFile(root / "files" / path, bytes, pError)) {
            return false;
        }
    }
    const std::string modText = "name=" + (request.name.empty() ? files.internalName : request.name) +
                                "\ndescription=A new galaxy made in the Petari level editor\neditor=1\n";
    if (!writeFile(root / "galaxy.json", Formats::Bytes(files.galaxyJson.begin(), files.galaxyJson.end()), pError) ||
        !writeFile(root / "mod.txt", Formats::Bytes(modText.begin(), modText.end()), pError)) {
        return false;
    }
    std::string enableError;
    if (!PetariNative::Platform::ModFolder::setEnabled(files.internalName, true, &enableError)) {
        *pError = "the galaxy is written, but its mod could not be switched on: " + enableError;
        return false;
    }
    for (const std::string& line : files.log) {
        std::fprintf(stderr, "[editor] new galaxy: %s\n", line.c_str());
    }
    // The restarted game opens the same file and goes straight into the galaxy, in Edit.
    setenv("PETARI_OPEN_STAGE", files.internalName.c_str(), 1);
    setenv("PETARI_OPEN_TITLE", (request.name.empty() ? files.internalName : request.name).c_str(), 1);
    setenv("PETARI_OPEN_FILE", std::to_string(GameDataFunction::getUserFileIndex()).c_str(), 1);
    setenv("PETARI_OPEN_EDIT", "1", 1);
    if (std::getenv("PETARI_STAGE") != nullptr) {
        setenv("PETARI_STAGE", files.internalName.c_str(), 1);  // a test fixture's restart enters through its synthetic entry
        setenv("PETARI_SCENARIO", "1", 1);
    }
    Core::reportNotice("Creating " + (request.name.empty() ? files.internalName : request.name) + "... Petari restarts into it");
    std::fprintf(stderr, "[editor] new galaxy %s written to mods/%s; restarting into it\n", files.internalName.c_str(), files.internalName.c_str());
    gRestartIn = 3;
    return true;
}

void newGalaxyTick() {
    if (gRestartIn > 0 && --gRestartIn == 0) {
        OSRestart(0);
    }
}

}  // namespace PetariEditor::Runtime
