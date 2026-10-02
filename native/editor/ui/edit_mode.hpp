// Edit mode, UI side (edit_mode.cpp). Internal to petari_editor_ui.
#pragma once

namespace PetariEditor::Ui {

// Once per frame, before the panels: follows the game's Edit nerve (panels shown while editing,
// Runtime told) and turns mouse, trackpad and keys over the viewport into editor-camera input.
void updateEditMode(float windowWidth, float windowHeight);

}  // namespace PetariEditor::Ui
