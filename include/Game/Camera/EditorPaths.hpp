#pragma once
#ifdef PETARI_NATIVE
#include "Game/Camera/EditorPicking.hpp"
namespace EditorPaths {
    void update(const EditorPicking::View& view);
    void draw();
}  // namespace EditorPaths
#endif
