// Saving an edit as a mod from the game (TECH.md 5.1): ModelIO::saveToDirectory (shared with the
// headless tools) into <user>/mods/<Name>, switched on for the next start. During the session the
// edits already show (reloads inject them).
#include "save_mod.hpp"

#include "model_io.hpp"

#include <petari/platform/mod_folder.hpp>

#include <filesystem>

namespace PetariEditor::Runtime {
namespace fs = std::filesystem;

bool saveMod(const EditModel& model, const std::string& modName, bool fullArchives, std::string* pNotice, std::string* pError) {
    if (!ModelIO::validModName(modName)) {
        *pError = "a mod name uses letters, digits, spaces, '-', '_' and '.'";
        return false;
    }
    const fs::path user = PetariNative::Platform::ModFolder::userDirectory();
    if (user.empty()) {
        *pError = "no user folder";
        return false;
    }
    std::string summary;
    if (!ModelIO::saveToDirectory(model, user / "mods" / modName, fullArchives, &summary, pError)) {
        return false;
    }
    std::string enableError;
    PetariNative::Platform::ModFolder::setEnabled(modName, true, &enableError);
    *pNotice = "Saved " + summary + " to mods/" + modName +
               (enableError.empty() ? " (on from the next start)" : " (turn it on in the Mods page: " + enableError + ")");
    return true;
}

}  // namespace PetariEditor::Runtime
