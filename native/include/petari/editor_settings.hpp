#pragma once
// The level editor's Edit mode, host side (docs/dev/level-editor/UX.md section 3): the
// toggle, the input gate, and the editor camera's input feed. The editor is off unless the
// Mods page's "Level editor" is on (Mods::Mod::LevelEditor); while off nothing is
// accumulated and every input keeps its meaning (Tab still tilts the remote).

namespace PetariNative::EditorSettings {

bool enabled();  // the Mods switch, any thread

// True from a toggle until the game has answered it, and while the game is in Edit mode:
// the host then sends the game no input. A toggle no scene answers lapses after 0.3 s.
bool capturing();
bool active();          // the game is in Edit mode
void toggle();          // a Tab tap: enter Edit where the pause menu could open, or leave it
void requestEnter();    // re-enter after a reload (Runtime's resumeEdit), keeping the camera
void requestLeave();    // leave for Play (Runtime's leaveEdit)

// The editor camera's input (the UI calls these from the overlay while active).
void orbit(float yawDegrees, float pitchDegrees);
void look(float yawDegrees, float pitchDegrees);
void pan(float right, float up);  // viewport heights
void dolly(float steps);          // + = closer
void move(float right, float forward, float up);  // held, -1..1
void speed(float multiplier);
void repivot();
void cursor(float x, float y);    // -1..1 across the viewport, y up
void frame();
void setWorldUp(bool world);
bool worldUp();

void resetForTesting();

}  // namespace PetariNative::EditorSettings
