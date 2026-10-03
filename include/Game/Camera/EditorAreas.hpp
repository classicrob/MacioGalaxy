#pragma once
#ifdef PETARI_NATIVE
#include "Game/Camera/EditorPicking.hpp"
#include <petari/editor/areas.hpp>
namespace EditorAreas {
    void update(const EditorPicking::View& view);
    void draw();
    PetariEditor::RowId pick(PetariEditor::Vec3 eye, PetariEditor::Vec3 ray, float* distance);
    bool bounds(PetariEditor::RowId row, float center[3], float* radius);
    void registerHooks();
}  // namespace EditorAreas
#endif
