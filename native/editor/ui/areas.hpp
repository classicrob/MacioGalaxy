#pragma once
#include <petari/editor/edit_api.hpp>
#include <string>
namespace PetariEditor::Ui {
    void drawAreasToggle(bool cameras = false);
    void drawAreasInspector(const ModelView&, const std::vector< RowId >&);
    bool drawAreasOverlay(float x, float y, float width, float height);
    // A camera type in words: "CAM_TYPE_FOLLOW" -> "Follow" ("Special camera" for the rest).
    const char* cameraTypeName(const std::string& type);
}  // namespace PetariEditor::Ui
