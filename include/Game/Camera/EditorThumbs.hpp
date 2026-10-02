#pragma once

#ifdef PETARI_NATIVE
// The level editor's thumbnails (docs/dev/level-editor/TECH.md 3.5): one requested class per frame, its
// placed example's own model drawn in an offscreen pass (Aurora's GXCreateFrameBuffer) and copied out.
namespace EditorThumbs {
    void render();  // at the start of GameScene::draw3D, only in Edit mode
}  // namespace EditorThumbs
#endif
