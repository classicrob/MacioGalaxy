#pragma once
#include <petari/editor/edit_api.hpp>
namespace PetariEditor::Ui {
    bool drawPathsViewport(float x, float y, float width, float height);
    void drawPathsInspector(const ModelView& model, const std::vector< RowId >& rows);
    void drawPathsToggle();
    void drawPathsWireOverlay(float x, float y, float width, float height);
    bool pathsDelete();
    void beginPathPick(RowId row);
    void beginNewPath(RowId rider = {});
}  // namespace PetariEditor::Ui
