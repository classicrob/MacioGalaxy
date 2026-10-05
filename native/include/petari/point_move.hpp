#pragma once
// Point to move mod (Mods::Mod::PointToMove, native/MODS.md "Point to move").
// While the left mouse button is held in normal play, Mario runs toward the
// point under the cursor: the game side (Game/System/PointMove.hpp) publishes
// a stick vector each frame, and the remote's Nunchuk stick follows it while
// no stick key is held. A left press claimed this way sends no B until it is
// released; a press outside normal play (menus, demos) is B as before.

#ifdef __cplusplus
namespace PetariNative::PointMove {

// Screen distance from Mario to the pointer, in screen heights: closer gives no
// movement; anywhere farther, Mario always runs (full stick toward the pointer).
constexpr float kDeadZone = 0.03f;

// The stick (x right, y up, length 0 or 1) for a pointer dx, dy (screen units,
// y down) away from Mario on a screen screenHeight units tall.
void stickFor(float dx, float dy, float screenHeight, float* x, float* y);

// Input side, with the input layer's lock held. The left button went down or
// up: a press is claimed while the mod is on and the game is steerable.
void leftButton(bool down);
bool claimsLeft();
// RemoteModel::stickVector: the published stick while the left button
// (leftHeld) is down and claimed. False otherwise (the usual stick applies).
bool stick(bool leftHeld, float* x, float* y);

void resetForTesting();

}  // namespace PetariNative::PointMove
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Game side, once per frame: whether Mario is in normal control (a left press
// is then claimed, wherever the pointer is), and the stick toward the pointer
// (x right, y up; zero while the pointer is off the screen).
void petari_point_move_publish(bool steerable, float stickX, float stickY);
// The left button is held and claimed (the smoke probe).
bool petari_point_move_claimed(void);
// Game side, with petari_point_move_publish while steerable: Mario's projected
// point and the pointer, in KPAD space (-1..1 across the game image, y down).
void petari_point_move_publish_points(float marioX, float marioY, float pointerX, float pointerY);
// Any thread (the steering cue in the overlay): true while the left button is
// held, claimed and Mario steerable, with the two points of the last publish.
bool petari_point_move_cue(float* marioX, float* marioY, float* pointerX, float* pointerY);

#ifdef __cplusplus
}
#endif
