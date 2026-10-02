// Saving an edit as a Full-archive mod (TECH.md 5.1): the edited zone archives, Yaz0-compressed as
// the community's tools write them, in <user>/mods/<Name>/files/StageData/, which today's mod
// loader reads (native/MODS.md). The mod is switched on for the next start; during the session the
// edits already show (reloads inject them).
#include "edit_model.hpp"
#include "save_mod.hpp"

#include <petari/platform/mod_folder.hpp>

#include <filesystem>
#include <set>
#include <fstream>
#include <system_error>

namespace PetariEditor::Runtime {
namespace fs = std::filesystem;

namespace {
    bool validName(const std::string& name) {
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

bool saveFullArchiveMod(const EditModel& model, const std::string& modName, std::string* pNotice, std::string* pError) {
    if (!validName(modName)) {
        *pError = "a mod name uses letters, digits, spaces, '-', '_' and '.'";
        return false;
    }
    const fs::path user = PetariNative::Platform::ModFolder::userDirectory();
    if (user.empty()) {
        *pError = "no user folder";
        return false;
    }
    const fs::path root = user / "mods" / modName;
    std::error_code ec;
    if (fs::exists(root, ec) && !isEditorMod(root)) {
        *pError = "mods/" + modName + " exists and was not made by the editor";
        return false;
    }

    // Every zone archive that differs from the disc; the others are removed from the mod, so an
    // edit reverted since the last save takes effect.
    int written = 0;
    std::set< std::string > changed;
    for (auto& [discPath, plain] : model.changedArchives()) {
        if (!writeAtomically(root / "files" / discPath.substr(1), Formats::yaz0Encode(plain), pError)) {
            return false;
        }
        changed.insert(discPath);
        written++;
    }
    for (int zone = 0; zone < model.zoneCount(); zone++) {
        const std::string discPath = std::string("/StageData/") + model.zoneName(zone) + ".arc";
        if (!changed.count(discPath)) {
            fs::remove(root / "files" / discPath.substr(1), ec);
        }
    }
    if (!fs::exists(root / "mod.txt", ec)) {
        Formats::Bytes text;
        const std::string body = "name=" + modName + "\ndescription=" + model.galaxy() + ", edited in the Petari level editor\neditor=1\n";
        text.assign(body.begin(), body.end());
        if (!writeAtomically(root / "mod.txt", text, pError)) {
            return false;
        }
    }
    std::string enableError;
    PetariNative::Platform::ModFolder::setEnabled(modName, true, &enableError);
    *pNotice = "Saved " + std::to_string(written) + (written == 1 ? " archive" : " archives") + " to mods/" + modName +
               (enableError.empty() ? " (on from the next start)" : " (turn it on in the Mods page: " + enableError + ")");
    return true;
}

}  // namespace PetariEditor::Runtime
