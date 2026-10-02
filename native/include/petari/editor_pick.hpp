#pragma once
// The level editor's picking and selection (docs/dev/level-editor/UX.md section 6, TECH.md 3.3).
// The UI asks (hover, click) from the overlay; the game's Edit nerve answers on the game thread,
// where actors may be read, and publishes what the UI draws and shows. Any thread; a mutex inside.

#include <cstdint>
#include <vector>

namespace PetariNative::EditorPick {

struct Item {
    std::uint64_t row = 0;       // PetariEditor::RowId (0: an actor the EditModel does not know yet)
    std::uintptr_t actor = 0;    // the live actor (identity only; never dereferenced off the game thread)
    char name[64] = {};          // the object's name (the row's, else the actor's class)
    float center[3] = {};        // world
    float radius = 0.0f;
    bool onScreen = false;
    float screenX = 0.0f;        // viewport -1..1 (x right, y up)
    float screenY = 0.0f;
    float screenRadius = 0.0f;   // in viewport heights
};

enum class Click : std::uint8_t { None, Replace, Add, Toggle };

// UI, overlay: what the pointer does this frame.
void setHoverWanted(bool wanted);  // the pointer is over the viewport (not dragging the camera)
void click(Click mode);            // a click over the viewport
void clearSelection();             // Esc, or Edit mode left
void selectByName(const char* name);  // select the first object with this row or class name (search, tests)
void selectRow(std::uint64_t row, Click mode);  // select a row's object (the Changes and Outliner lists)
void placeMarker();  // M: the play marker onto the ground under the cursor

// Game, Edit nerve: the requests, then the answers.
bool hoverWanted();
Click takeClick();
bool takeClear();
bool takeSelectByName(char* name, int size);
bool takeSelectRow(std::uint64_t* row, Click* mode);
bool takePlaceMarker();
void publish(const Item* hover, const std::vector<Item>& selection);

// UI: the last answers.
bool hover(Item* out);
std::vector<Item> selection();

void resetForTesting();

}  // namespace PetariNative::EditorPick

// The transform gizmo (docs/dev/level-editor/UX.md section 6): the UI says what the mouse and keys
// do; the game's Edit nerve moves the selection and publishes the handles to draw.
namespace PetariNative::EditorGizmo {

enum class Mode : std::uint8_t { Select, Move, Rotate, Scale };  // Q W E R
enum class Frame : std::uint8_t { Gravity, Local, World };       // the gizmo's axes (","" cycles)
enum class Ground : std::uint8_t { Drop = 1, StandUp = 2, Both = 3 };  // End, Shift+End, Cmd+End

void setMode(Mode mode);
Mode mode();
void setFrame(Frame frame);
Frame frame();

// UI, overlay: the left button over the viewport (not an Option-drag, which orbits).
void press();
void release();
void setSnapSuspended(bool suspended);  // Cmd held: no snapping for this drag
void ground(Ground what);

struct Take {
    bool pressed = false, released = false, snapSuspended = false;
    int ground = 0;  // a Ground value, 0 none
};
Take take();  // game, once per frame

// What the game publishes for the UI to draw: lines in viewport coordinates (-1..1, y up).
struct Line {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::uint32_t color = 0xFFFFFFFF;  // ImGui's ABGR
    float width = 2.0f;
};
struct View {
    bool visible = false;
    bool dragging = false;   // a gizmo or object drag is under way (the UI holds back the click)
    std::vector<Line> lines;
    float originX = 0, originY = 0;
    char status[96] = {};    // for the footer: "Move  X +120.0" ...
    // The play marker (UX section 3): where Play puts Mario.
    bool marker = false;
    float markerX = 0, markerY = 0, markerRadius = 0;  // viewport coordinates; radius in viewport heights
};
void publish(const View& view);
View view();

void resetForTesting();

}  // namespace PetariNative::EditorGizmo
