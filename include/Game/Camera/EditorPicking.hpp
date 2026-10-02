#pragma once

#ifdef PETARI_NATIVE
// The level editor's picking (docs/dev/level-editor/TECH.md 3.3): what is under the cursor, on the
// game thread in the Edit nerve. Answers PetariNative::EditorPick's requests.
class LiveActor;

namespace EditorPicking {
    struct View {
        float eye[3], forward[3], up[3];
        float fovy, aspect;
        float cursorX, cursorY;  // -1..1 across the viewport, y up
    };
    // Once per Edit-mode frame, after the camera moved.
    void update(const View& view);
    // The world centre and radius of the selection (false while nothing is selected), for F.
    bool selectionBounds(float center[3], float* radius);
    // Edit mode left: drop the selection's actors (the rows are kept by the UI).
    void reset();
    // For drawing (game thread): the hovered actor (null: none) and the selection.
    LiveActor* hoverActor();
    int selectedCount();
    LiveActor* selectedAt(int index);
}  // namespace EditorPicking
#endif
