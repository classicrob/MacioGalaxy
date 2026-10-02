#pragma once
#include <string>

namespace PetariEditor {
class EditModel;
}

namespace PetariEditor::Runtime {

/// Saves the model's edits as the mod <user>/mods/<modName> and switches it on for the next start.
/// A patch (edits/<Galaxy>.json, TECH.md 5.3; no game data) unless `fullArchives`: then the edited
/// zone archives, Yaz0, in files/StageData/. Either form removes the other's files for this galaxy.
/// Refuses a folder the editor did not make. *pNotice: a one-line summary for the UI.
bool saveMod(const EditModel& model, const std::string& modName, bool fullArchives, std::string* pNotice, std::string* pError);

}  // namespace PetariEditor::Runtime
