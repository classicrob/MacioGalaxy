#pragma once
#include <string>

namespace PetariEditor {
class EditModel;
}

namespace PetariEditor::Runtime {

/// Writes the model's edited zone archives as a Full-archive mod in <user>/mods/<modName> and
/// switches it on for the next start. Refuses a folder the editor did not make. *pNotice: a
/// one-line summary for the UI.
bool saveFullArchiveMod(const EditModel& model, const std::string& modName, std::string* pNotice, std::string* pError);

}  // namespace PetariEditor::Runtime
