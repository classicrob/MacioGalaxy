#pragma once
#include <petari/editor/edit_api.hpp>
namespace PetariEditor::Ui {
    bool drawGravityOverlay(float x, float y, float width, float height);
    void drawGravityInspector(const ModelView& model, const std::vector< RowId >& rows);
}  // namespace PetariEditor::Ui
