#pragma once
// The level editor runtime's entry point in the frame (TECH.md 2.10, 10).

namespace PetariEditor::Runtime {

/// GameScene::update, every frame, before the scene's nerves (CPU baton held). Loads the
/// EditModel when an edit session starts, binds placed actors to their rows after each scene
/// build, drains the UI's commands into the model and SceneSync, refreshes status() and runs
/// the UI seam hook. Does nothing while the editor is off.
void tick();

}  // namespace PetariEditor::Runtime
