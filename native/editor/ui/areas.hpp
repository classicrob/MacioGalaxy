#pragma once
#include <petari/editor/edit_api.hpp>
namespace PetariEditor::Ui {
    void drawAreasToggle();
    void drawAreasInspector(const ModelView&, const std::vector< RowId >&);
    bool drawAreasOverlay(float x, float y, float width, float height);
}  // namespace PetariEditor::Ui
