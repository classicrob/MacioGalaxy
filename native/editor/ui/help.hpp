#pragma once
#include <petari/editor/ui.hpp>

struct ImDrawList;

namespace PetariEditor::Ui {
    // Returns true when Escape closes help, so it cannot also cancel a viewport tool.
    bool updateHelpInput();
    void drawHelpOverlay();
    // Viewport marks stay below the help panel while it is open.
    ImDrawList* viewportDrawList();
}  // namespace PetariEditor::Ui
