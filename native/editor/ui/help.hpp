#pragma once
#include <petari/editor/ui.hpp>

namespace PetariEditor::Ui {
    // Returns true when Escape closes help, so it cannot also cancel a viewport tool.
    bool updateHelpInput();
    void drawHelpOverlay();
}  // namespace PetariEditor::Ui
