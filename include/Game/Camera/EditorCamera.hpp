#pragma once

#ifdef PETARI_NATIVE
// The level editor's Edit mode (docs/dev/level-editor/UX.md sections 3 and 5): GameScene freezes like
// its pause menu (no movement list), the HUD is not drawn, and the editor camera orbits, pans, zooms
// and flies over the frozen scene with "up" following gravity at its pivot. Leaving restores the
// game's view; the editor camera's own view is kept for the next time (after a reload too).
namespace EditorCamera {
    // Game thread, once per GameScene update while not editing: true when the editor asks to
    // enter (a Tab tap, or re-entry after a reload) and the scene can pause now (else refused).
    bool takeEnterRequest(bool canPause);
    void start();
    // One frame in Edit mode: move the camera. True when the editor leaves (Tab, or Play).
    bool update();
    void end();
    bool isActive();
    // A new GameScene: Edit mode ends without restoring a view (the old scene is gone); the editor's own
    // view is kept for re-entry. No-op when not editing.
    void abandon();
}  // namespace EditorCamera
#endif
