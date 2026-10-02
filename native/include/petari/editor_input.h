#ifndef PETARI_EDITOR_INPUT_H
#define PETARI_EDITOR_INPUT_H
/* The level editor's Edit mode (docs/dev/level-editor/UX.md section 3 and 5): what the
 * host hands the game's Edit nerve and editor camera each frame. Plain C, so SDK-side game
 * code can include it. Angles in degrees. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PetariEditorInput {
    int enabled;         /* the Mods page's "Level editor" is on */
    int toggle;          /* Edit/Play taps since the last call (Tab) */
    int enter;           /* the editor asks to (re-)enter Edit, keeping its camera (after a reload) */
    int leave;           /* the editor asks to leave Edit (Play) */
    /* Camera, accumulated since the last call. */
    float orbitYaw;      /* orbit about the pivot */
    float orbitPitch;
    float lookYaw;       /* turn in place (fly) */
    float lookPitch;
    float panRight;      /* pan, in viewport heights at the pivot's distance */
    float panUp;
    float dolly;         /* steps toward the point under the cursor (+ = closer) */
    float moveRight;     /* fly, held keys -1..1 */
    float moveForward;
    float moveUp;
    float speed;         /* fly speed multiplier (1 normal) */
    int repivot;         /* orbit around the point under the cursor from now on */
    float cursorX;       /* the cursor across the viewport, -1..1 (x right, y up) */
    float cursorY;
    int frame;           /* F: frame the selection (Mario while nothing is selected) */
    int worldUp;         /* 1: up is world +Y; 0: up follows gravity at the pivot */
    int playFrom;        /* what a toggle in Edit mode plays: 0 the play marker, 1 resume, 2 the mission start */
} PetariEditorInput;

/* Game thread, once per frame: the input since the last call. */
void petari_editor_take_input(PetariEditorInput* out);

/* Game thread: the Edit nerve was entered (1) or left (0). While active the host sends the
 * game no input. */
void petari_editor_set_active(int active);

#ifdef __cplusplus
}
#endif

#endif
