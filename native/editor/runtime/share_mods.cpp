// Sharing and installing mods from the editor's sheets (edit_api.hpp, "Sharing"): the pure zip
// work is petari/editor/share.hpp; this finds the user's mods folder and turns installs on.
#include <petari/editor/edit_api.hpp>
#include <petari/host_allocation.hpp>
#include <petari/platform/mod_folder.hpp>

namespace PetariEditor {
namespace fs = std::filesystem;

namespace {
    fs::path modsDirectory(std::string* pError) {
        const fs::path user = PetariNative::Platform::ModFolder::userDirectory();
        if (user.empty()) *pError = "no user folder";
        return user.empty() ? fs::path() : user / "mods";
    }
}  // namespace

bool shareMod(const std::string& modName, const Share::Info& info, const std::string& zipPath, std::string* pError) {
    PetariNative::HostAllocationScope host;
    const fs::path mods = modsDirectory(pError);
    return !mods.empty() && Share::exportZip(mods / modName, info, zipPath, pError);
}

std::optional< Share::Preview > inspectModZip(const std::string& zipPath, std::string* pError) {
    PetariNative::HostAllocationScope host;
    return Share::inspectZip(zipPath, pError);
}

bool installModZip(const std::string& zipPath, bool replace, std::string* pFolder, std::string* pError) {
    PetariNative::HostAllocationScope host;
    const fs::path mods = modsDirectory(pError);
    if (mods.empty() || !Share::installZip(zipPath, mods, replace, pFolder, pError)) {
        return false;
    }
    std::string enableError;
    if (!PetariNative::Platform::ModFolder::setEnabled(*pFolder, true, &enableError)) {
        *pError = "installed, but not turned on: " + enableError;
        return false;
    }
    return true;
}

}  // namespace PetariEditor
