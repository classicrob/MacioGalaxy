#pragma once
// Runtime-side access to the editor session behind edit_api.hpp (not for the UI).
#include <petari/editor/edit_api.hpp>

namespace PetariEditor::Core {

/// The model the UI reads (null: none loaded). Owned by Runtime.
void setModel(const ModelView* pModel);
/// Commands queued since the last drain, in order. Seam only.
std::vector< Command > drainCommands();
void setMode(Mode mode);
Status& mutableStatus();
std::vector< Check >& mutableChecks();
/// Reports a failed command to the UI (Status::lastError, errorSerial).
void reportError(const std::string& message);
/// Reports a success worth a toast (Status::notice, noticeSerial).
void reportNotice(const std::string& message);
/// Calls the UI seam hook, if any. Seam only, after the drain and SceneSync.
void runUiSeamHook();
const UiCallbacks& uiCallbacks();

/// The row <-> actor binding SceneSync maintains (TECH.md 2.6).
void bindActor(RowId row, LiveActor* pActor);
void unbindRow(RowId row);
void clearBindings();

/// The new-galaxy queries behind edit_api.hpp, supplied by the runtime (new_galaxy_session.cpp),
/// which reads the mounted disc; empty answers without it (the UI's tests).
struct NewGalaxyServices {
    const NewGalaxyChoices& (*choices)() = nullptr;
    std::string (*internalName)(const std::string& name) = nullptr;
    std::string (*check)(const std::string& internalName) = nullptr;
};
void setNewGalaxyServices(const NewGalaxyServices& services);

}  // namespace PetariEditor::Core
