#pragma once
#include <petari/editor/edit_api.hpp>
namespace PetariEditor::Ui {
    void drawArrayInspector(const ModelView& model, const std::vector< RowId >& rows);
    bool drawArrayOverlay(float x, float y, float width, float height);
}  // namespace PetariEditor::Ui
