#pragma once
// Remapping controls from the Home menu (native/CONTROLS.md "Changing
// controls"): capturing the next pressed input, editing one action's inputs
// with conflict handling, and the per-user controls.txt.
//
// The Controls page shows each action with four slots: two for keyboard and
// mouse inputs and two for controller buttons, in the order the action's
// inputs are bound (Bindings::inputs). A third input of one kind stays bound
// and keeps its place, but has no slot of its own.

#include <filesystem>
#include <string>
#include <vector>

#include "petari/input.hpp"

namespace PetariNative::Input {

// --- Capture: the next press is reported here instead of reaching the game ---

enum class CaptureState : std::uint8_t {
    Idle,       // no capture
    Waiting,    // beginCapture() was called; nothing pressed yet
    Captured,   // a key, mouse button or controller button went down
    Cancelled,  // Escape went down
};

// Starts a capture. Until a press arrives, the next down edge of any key
// (Escape cancels), mouse button or controller button is not sent to the
// game, and neither is its release; OS key repeats are swallowed too. Inputs
// already held keep working and are released normally. Any thread.
void beginCapture();
// Stops a capture that has not seen a press. Any thread.
void cancelCapture();
// The capture's state; Captured (with *input) and Cancelled are reported once,
// then the state is Idle again. Any thread.
CaptureState captureState(Binding* input);

namespace Remap {

constexpr int kSlots = 4;        // keyboard/mouse 0 and 1, controller 2 and 3
constexpr int kFirstPadSlot = 2;

inline bool padSlot(int slot) {
    return slot >= kFirstPadSlot;
}

// The input in a slot, if it has one.
bool slotInput(const Bindings& bindings, Action action, int slot, Binding* input);

// The other actions an input is also bound to.
std::vector<Action> boundElsewhere(const Bindings& bindings, Action action, Binding input);
// Like boundElsewhere, without the sharing the defaults have themselves
// (RB is both Spin and the Cappy mod's throw): what the page marks.
std::vector<Action> conflicts(const Bindings& bindings, Action action, Binding input);

// The Home menu stays reachable: Home keeps a keyboard or mouse input.
bool homeReachable(const Bindings& bindings);

enum class Resolve : std::uint8_t {
    Check,     // change nothing if another action has the input: report Conflict
    Swap,      // the other actions get this slot's old input instead (or lose it, if the slot was empty)
    KeepBoth,  // leave the other actions as they are; the input drives both
};

enum class Result : std::uint8_t {
    Done,
    Unchanged,        // the slot already had that input
    Conflict,         // Resolve::Check and another action has it (see boundElsewhere)
    HomeUnreachable,  // refused: Home would lose its last keyboard and mouse input
};

// Puts an input in an action's slot, replacing what was there. An input of the
// other kind (a controller button for a keyboard slot, or a key for a
// controller slot) goes to the same-numbered slot of its own kind. If the
// action already has the input in its other slot, the two slots trade places.
Result assign(Bindings& bindings, Action action, int slot, Binding input, Resolve resolve);
// Empties a slot.
Result clearSlot(Bindings& bindings, Action action, int slot);
// The action's inputs as in Bindings::defaults(); other actions are untouched.
Result resetAction(Bindings& bindings, Action action);

// --- controls.txt ---

// Reads the file into defaults-plus-overrides (Bindings::parse). A missing
// file gives the defaults with no message. An unreadable or invalid file gives
// the defaults, returns false and says why in *message; the file is then
// renamed to controls.txt.bad the first time save() replaces it. A file that
// leaves Home without a keyboard or mouse input gets the default Home inputs
// back, with a message (returns true). The path is remembered for save().
bool load(const std::filesystem::path& file, Bindings* bindings, std::string* message);
// Writes all bindings to the remembered file (atomically), with a comment
// header. False with a message if no file was loaded or writing fails.
bool save(const Bindings& bindings, std::string* error);
// The file load() read (empty before).
std::filesystem::path file();
// The last load()'s message, for the Controls page ("" if none).
std::string loadMessage();

}  // namespace Remap
}  // namespace PetariNative::Input
