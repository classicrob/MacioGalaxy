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

// Game, Edit nerve: the requests, then the answers.
bool hoverWanted();
Click takeClick();
bool takeClear();
void publish(const Item* hover, const std::vector<Item>& selection);

// UI: the last answers.
bool hover(Item* out);
std::vector<Item> selection();

void resetForTesting();

}  // namespace PetariNative::EditorPick
