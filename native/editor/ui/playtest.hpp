#pragma once
#include <petari/editor/edit_api.hpp>
namespace PetariEditor::Ui {
    void updatePlaytestInput();
    void drawPlaytestToggle();
    void drawPlaytestInspector(const std::vector< RowId >& rows);
    bool drawPlaytestFooter();
}  // namespace PetariEditor::Ui
