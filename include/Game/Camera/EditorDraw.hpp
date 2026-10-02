#pragma once

#ifdef PETARI_NATIVE
// The level editor's drawing in the scene (docs/dev/level-editor/TECH.md 3.1 and 3.4): the selection
// and the hovered object, drawn again over themselves in the selection's gold and the hover's cyan,
// with a faint x-ray where something is in front of them. Called at the end of GameScene::draw3D,
// before the Z buffer is cleared, only in Edit mode.
namespace EditorDraw {
    void draw3D();
}  // namespace EditorDraw
#endif
