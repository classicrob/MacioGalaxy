#pragma once
// New galaxies in the running editor (new_galaxy_session.cpp). Seam only, baton held.
#include <petari/editor/edit_api.hpp>
#include <string>

namespace PetariEditor::Runtime {

/// Supplies newGalaxyChoices() / checkNewGalaxyName() (edit_api.hpp) from the mounted disc.
void registerNewGalaxyServices();

/// Command::NewGalaxy: writes the galaxy as a switched-on mod and schedules the restart into it.
/// False and *pError when the name is taken, the session has unsaved edits the request doesn't
/// discard, or a file can't be written.
bool createNewGalaxy(const NewGalaxyRequest& request, bool unsaved, std::string* pError);

/// Each seam: restarts the game a few seams after a successful create (the notice shows first).
void newGalaxyTick();

}  // namespace PetariEditor::Runtime
