// The play view (friction #14): a small corner picture of what Play would show from the play marker, whatever is
// selected. The game picks and renders the camera (src/Game/Camera/EditorCameraPreview.cpp); this draws it.
#pragma once

namespace PetariEditor::Ui {

void drawPlayViewToggle();  // the View menu's line (4)
// The draggable corner picture over the viewport; true while the pointer is over it.
bool drawPlayViewOverlay(float x, float y, float width, float height);

}  // namespace PetariEditor::Ui
