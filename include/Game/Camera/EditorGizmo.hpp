#pragma once

#ifdef PETARI_NATIVE
#include "Game/Camera/EditorPicking.hpp"

// The level editor's transform gizmo (docs/dev/level-editor/UX.md section 6): move along the gravity
// frame's axes, rotate on its rings, scale, slide over the ground (dragging the centre or the object),
// and End to drop along gravity. Runs on the game thread in the Edit nerve: previews through Runtime's
// Command::Preview while dragging and records one Apply op when the drag ends.
namespace EditorGizmo {
    void update(const EditorPicking::View& view);  // after picking, each Edit-mode frame
    bool dragging();
    void reset();  // Edit mode left
}  // namespace EditorGizmo
#endif
