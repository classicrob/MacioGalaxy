#pragma once
// The level editor runtime's entry point in the frame (TECH.md 2.10, 10).

namespace PetariEditor::Runtime {

/// GameScene::update, every frame, before the scene's nerves (CPU baton held). Loads the
/// EditModel when an edit session starts, binds placed actors to their rows after each scene
/// build, drains the UI's commands into the model and SceneSync, refreshes status() and runs
/// the UI seam hook. Does nothing while the editor is off.
/// playable: the scene is in normal play where the pause menu could open (a reload is complete
/// and Mario can be moved).
void tick(bool playable);

/// A same-stage rebuild with the editor's fast path (no lives counter, shader gate or wipe;
/// ~0.3-0.5 s): with an edit session in Edit mode it is the editor's Reload (edits included, back
/// in Edit after); without one, a plain restart of the stage (for test drivers). fromStart: at the
/// mission's start instead of the restart point. False when a reload is already in flight or no
/// stage is loaded. Game thread (GameScene::update).
bool reloadScene(bool fromStart);
/// True from reloadScene() (or the editor's Reload) until the new scene is playable.
bool reloading();

}  // namespace PetariEditor::Runtime
