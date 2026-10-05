// Remapping from the Home menu (petari/input_remap.hpp): capture of the next
// press, slots, conflicts (check, swap, keep both), the Home guard, resets, and
// controls.txt (round trip, missing, invalid, Home repair, the .bad copy).

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "petari/input.hpp"
#include "petari/input_remap.hpp"

namespace In = PetariNative::Input;
namespace Remap = PetariNative::Input::Remap;
using In::Action;
using In::Binding;
using Remap::Resolve;
using Remap::Result;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const std::string& message) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    }
}

const Binding kF = Binding::key(In::Key::F);
const Binding kR = Binding::key(In::Key::R);
const Binding kG = Binding::key(In::Key::G);
const Binding kSpace = Binding::key(In::Key::Space);
const Binding kF1 = Binding::key(In::Key::F1);
const Binding kGrave = Binding::key(In::Key::Grave);
const Binding kEscape = Binding::key(In::Key::Escape);
const Binding kNorth = Binding::pad(In::PadButton::North);
const Binding kWest = Binding::pad(In::PadButton::West);

bool has(const In::Bindings& b, Action action, Binding input) {
    for (const Binding& bound : b.inputs(action)) {
        if (bound == input) return true;
    }
    return false;
}

Binding slot(const In::Bindings& b, Action action, int index) {
    Binding input{In::Binding::Device::Key, 0};
    Remap::slotInput(b, action, index, &input);
    return input;
}

void testSlots() {
    const In::Bindings b = In::Bindings::defaults();
    check(slot(b, Action::A, 0) == kSpace && slot(b, Action::A, 1) == Binding::mouse(In::MouseButton::Right),
          "A: Space and Right mouse in the keyboard slots");
    check(slot(b, Action::A, 2) == Binding::pad(In::PadButton::South), "A: South in the first controller slot");
    Binding none;
    check(!Remap::slotInput(b, Action::A, 3, &none), "A has no second controller button");
    check(!Remap::slotInput(b, Action::A, 4, &none) && !Remap::slotInput(b, Action::A, -1, &none), "out-of-range slots");
}

void testAssign() {
    In::Bindings b = In::Bindings::defaults();
    // A free key replaces the slot.
    check(Remap::assign(b, Action::Shake, 0, kG, Resolve::Check) == Result::Conflict, "G is the Collect Star Bits mod's");
    check(Remap::assign(b, Action::Shake, 0, Binding::key(In::Key::Y), Resolve::Check) == Result::Done &&
              slot(b, Action::Shake, 0) == Binding::key(In::Key::Y) && !has(b, Action::Shake, kF),
          "a free key replaces the slot's input");
    check(Remap::assign(b, Action::Shake, 0, Binding::key(In::Key::Y), Resolve::Check) == Result::Unchanged,
          "the same input again changes nothing");

    // Conflict: Cap throw on F, which is Spin.
    b = In::Bindings::defaults();
    const In::Bindings before = b;
    check(Remap::assign(b, Action::ModCappyThrow, 0, kF, Resolve::Check) == Result::Conflict, "F is Spin's: a conflict");
    check(b.serialize() == before.serialize(), "a conflict changes nothing");
    const auto others = Remap::boundElsewhere(b, Action::ModCappyThrow, kF);
    check(others.size() == 1 && others[0] == Action::Shake, "the conflict names Spin");

    In::Bindings swapped = b;
    check(Remap::assign(swapped, Action::ModCappyThrow, 0, kF, Resolve::Swap) == Result::Done, "swap");
    check(slot(swapped, Action::ModCappyThrow, 0) == kF && slot(swapped, Action::Shake, 0) == kR && !has(swapped, Action::Shake, kF),
          "swap: Cap throw gets F, Spin gets R in F's place");
    check(has(swapped, Action::Shake, Binding::pad(In::PadButton::West)), "swap leaves Spin's other inputs");

    In::Bindings both = b;
    check(Remap::assign(both, Action::ModCappyThrow, 0, kF, Resolve::KeepBoth) == Result::Done &&
              has(both, Action::ModCappyThrow, kF) && has(both, Action::Shake, kF) && !has(both, Action::ModCappyThrow, kR),
          "keep both: F drives both");

    // Swap into an empty slot: the other action just loses the input.
    In::Bindings empty = In::Bindings::defaults();
    check(Remap::assign(empty, Action::One, 1, kF, Resolve::Swap) == Result::Done && has(empty, Action::One, kF) &&
              !has(empty, Action::Shake, kF) && has(empty, Action::One, Binding::key(In::Key::Num1)),
          "swap into an empty slot: the other action loses the input");

    // The other kind goes to its own column.
    In::Bindings kinds = In::Bindings::defaults();
    check(Remap::assign(kinds, Action::Walk, 0, Binding::pad(In::PadButton::LeftStick), Resolve::KeepBoth) == Result::Done &&
              slot(kinds, Action::Walk, 2) == Binding::pad(In::PadButton::LeftStick) &&
              slot(kinds, Action::Walk, 0) == Binding::key(In::Key::LeftAlt),
          "a controller button pressed for a keyboard slot goes to the controller slot");

    // Already in the other slot of the action: the slots trade places.
    In::Bindings trade = In::Bindings::defaults();
    check(Remap::assign(trade, Action::A, 0, Binding::mouse(In::MouseButton::Right), Resolve::Check) == Result::Done &&
              slot(trade, Action::A, 0) == Binding::mouse(In::MouseButton::Right) && slot(trade, Action::A, 1) == kSpace,
          "an input in the action's other slot trades places");

    // Controller slots and default sharing.
    In::Bindings pad = In::Bindings::defaults();
    check(Remap::conflicts(pad, Action::ModCappyThrow, Binding::pad(In::PadButton::RightShoulder)).empty(),
          "RB shared by Spin and the cap throw is not marked: the defaults share it");
    check(!Remap::conflicts(pad, Action::Shake, kNorth).empty(), "North is Start's: marked");
    check(Remap::assign(pad, Action::Shake, 3, kNorth, Resolve::Swap) == Result::Done &&
              slot(pad, Action::Shake, 3) == kNorth && has(pad, Action::Start, Binding::pad(In::PadButton::RightShoulder)),
          "controller swap: Start gets RB");
}

void testHomeGuard() {
    In::Bindings b = In::Bindings::defaults();
    check(Remap::homeReachable(b), "defaults reach Home");
    check(Remap::clearSlot(b, Action::Home, 0) == Result::Done, "clearing F1 leaves `");
    check(Remap::clearSlot(b, Action::Home, 0) == Result::HomeUnreachable && has(b, Action::Home, kGrave),
          "the last keyboard Home input cannot be cleared");
    check(Remap::clearSlot(b, Action::Home, 2) == Result::Done, "the controller's Guide can go");
    // Swapping the last Home key away to an action with an empty slot is refused.
    In::Bindings swap = b;
    check(Remap::assign(swap, Action::One, 1, kGrave, Resolve::Swap) == Result::HomeUnreachable && has(swap, Action::Home, kGrave),
          "swapping Home's last key away is refused");
    // ...but a swap that gives Home the other action's key is fine.
    check(Remap::assign(swap, Action::One, 0, kGrave, Resolve::Swap) == Result::Done &&
              has(swap, Action::Home, Binding::key(In::Key::Num1)) && !has(swap, Action::Home, kGrave),
          "a swap that gives Home another key is allowed");
    // Replacing Home's key with a controller button keeps the keyboard slot.
    In::Bindings onlyPad = b;
    check(Remap::assign(onlyPad, Action::Home, 0, Binding::pad(In::PadButton::Back), Resolve::KeepBoth) == Result::Done &&
              Remap::homeReachable(onlyPad),
          "a controller button for Home goes to its controller slot");
    check(Remap::resetAction(onlyPad, Action::Home) == Result::Done && has(onlyPad, Action::Home, kF1), "reset Home");
}

void testReset() {
    In::Bindings b = In::Bindings::defaults();
    Remap::assign(b, Action::Shake, 0, kF1, Resolve::Swap);  // Spin on F1, Home gets F
    check(has(b, Action::Home, kF) && has(b, Action::Shake, kF1), "setup: Spin and Home swapped");
    check(Remap::resetAction(b, Action::Shake) == Result::Done && slot(b, Action::Shake, 0) == kF, "reset Spin");
    check(has(b, Action::Home, kF), "resetting one action leaves the others");
    check(Remap::resetAction(b, Action::Shake) == Result::Unchanged, "resetting a default action changes nothing");
}

void testCapture() {
    In::resetForTesting();
    In::focusChanged(true);
    In::setBindings(In::Bindings::defaults());
    Binding got;
    check(In::captureState(&got) == In::CaptureState::Idle, "no capture at first");

    // Without a capture Space reaches the game.
    In::keyEvent(In::Key::Space, true, false);
    check(In::actionHeld(Action::A), "Space presses A");
    // A capture started while Space is held: its release still reaches the game.
    In::beginCapture();
    check(In::captureState(&got) == In::CaptureState::Waiting, "waiting");
    In::keyEvent(In::Key::Space, false, false);
    check(!In::actionHeld(Action::A), "a held input's release reaches the game during a capture");

    // The next press is captured and not sent to the game; nor its repeats or release.
    In::keyEvent(In::Key::F, true, false);
    check(!In::actionHeld(Action::Shake), "the captured press does not reach the game");
    check(In::captureState(&got) == In::CaptureState::Captured && got == kF, "F captured");
    check(In::captureState(&got) == In::CaptureState::Idle, "reported once");
    In::keyEvent(In::Key::F, true, true);
    check(!In::actionHeld(Action::Shake), "its OS repeat is swallowed");
    In::keyEvent(In::Key::F, false, false);
    In::keyEvent(In::Key::F, true, false);
    check(In::actionHeld(Action::Shake), "after its release F works again");
    In::keyEvent(In::Key::F, false, false);

    // Mouse and controller buttons.
    In::beginCapture();
    In::mouseButtonEvent(In::MouseButton::Left, true);
    check(In::captureState(&got) == In::CaptureState::Captured && got == Binding::mouse(In::MouseButton::Left) &&
              !In::actionHeld(Action::B),
          "a mouse button is captured");
    In::mouseButtonEvent(In::MouseButton::Left, false);
    In::beginCapture();
    In::padButtonEvent(In::PadButton::West, true);
    check(In::captureState(&got) == In::CaptureState::Captured && got == kWest && !In::actionHeld(Action::Shake),
          "a controller button is captured");
    In::padButtonEvent(In::PadButton::West, false);

    // Escape cancels, and neither its press nor its release pauses.
    In::beginCapture();
    In::keyEvent(In::Key::Escape, true, false);
    check(!In::actionHeld(Action::Plus), "Escape does not pause while capturing");
    check(In::captureState(&got) == In::CaptureState::Cancelled, "Escape cancels");
    In::keyEvent(In::Key::Escape, false, false);
    check(In::captureState(&got) == In::CaptureState::Idle, "idle after the cancel");

    // cancelCapture before a press: the next press reaches the game.
    In::beginCapture();
    In::cancelCapture();
    In::keyEvent(In::Key::F, true, false);
    check(In::actionHeld(Action::Shake) && In::captureState(&got) == In::CaptureState::Idle, "a cancelled capture takes nothing");
    In::keyEvent(In::Key::F, false, false);
    In::resetForTesting();
}

void testMenuScroll() {
    In::resetForTesting();
    In::focusChanged(true);
    check(In::takeMenuScroll().rows == 0, "no scrolling at first");
    In::menuWheel(1.0f);
    In::MenuScroll s = In::takeMenuScroll();
    check(s.rows == 3 && s.pages == 0, "a wheel notch is 3 rows toward the top");
    In::menuWheel(-2.0f);
    check(In::takeMenuScroll().rows == -6, "two notches down");
    // Trackpad: small precise deltas add up, the fraction carries over.
    for (int i = 0; i < 5; i++) In::menuWheel(0.3f);
    s = In::takeMenuScroll();
    check(s.rows == 0, "0.75 rows of trackpad: nothing yet");
    In::menuWheel(0.3f);
    check(In::takeMenuScroll().rows == 0, "0.9 rows");
    In::menuWheel(0.3f);
    check(In::takeMenuScroll().rows == 1, "1.05 rows: one row");
    check(In::takeMenuScroll().rows == 0, "taken once");
    In::menuWheel(1000.0f);
    check(In::takeMenuScroll().rows <= 100, "a burst is capped");
    // Page Up/Down keys, repeats included, and they still reach nothing else.
    In::keyEvent(78, true, false);
    In::keyEvent(78, true, true);
    In::keyEvent(78, false, false);
    In::keyEvent(75, true, false);
    In::keyEvent(75, false, false);
    check(In::takeMenuScroll().pages == 1, "Page Down twice (a repeat), Page Up once");
    // A Page key pressed during a capture is the captured input, not a page.
    In::beginCapture();
    In::keyEvent(78, true, false);
    In::Binding got;
    check(In::captureState(&got) == In::CaptureState::Captured && In::takeMenuScroll().pages == 0, "captured, not paged");
    In::keyEvent(78, false, false);
    In::resetForTesting();
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::trunc);
    out << text;
}

void testFile() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "petari-input-remap-tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "controls.txt";
    std::string message;
    In::Bindings b;

    // Missing: defaults, no message, and no file is created by loading.
    check(Remap::load(file, &b, &message) && message.empty() && b.serialize() == In::Bindings::defaults().serialize(),
          "a missing file gives the defaults");
    check(!std::filesystem::exists(file), "loading does not create the file");
    check(Remap::file() == file, "the file is remembered");

    // Round trip.
    Remap::assign(b, Action::ModCappyThrow, 0, kF, Resolve::Swap);
    Remap::clearSlot(b, Action::A, 1);
    Remap::assign(b, Action::Shake, 3, kNorth, Resolve::KeepBoth);
    std::string error;
    check(Remap::save(b, &error), "save: " + error);
    check(read(file).rfind("# Petari controls", 0) == 0, "the file starts with its comment");
    In::Bindings loaded;
    check(Remap::load(file, &loaded, &message) && message.empty() && loaded.serialize() == b.serialize(),
          "save then load gives the same bindings");
    check(!std::filesystem::exists(dir / "controls.txt.tmp"), "no temporary file left");

    // A file saved before the B key was a default: its old-default B line gets the B key.
    write(file, "B=Mouse:Left,Key:Backspace,Pad:East,Pad:RightTrigger\n");
    check(Remap::load(file, &loaded, &message) && message.empty() && has(loaded, Action::B, In::Binding::key(In::Key::B)) &&
              loaded.inputs(Action::B) == In::Bindings::defaults().inputs(Action::B),
          "an old-default B line gains the B key");
    write(file, "B=Mouse:Left,Key:Backspace,Pad:East,Pad:RightTrigger\nShake=Key:B\n");
    check(Remap::load(file, &loaded, &message) && !has(loaded, Action::B, In::Binding::key(In::Key::B)) &&
              has(loaded, Action::Shake, In::Binding::key(In::Key::B)),
          "a B key the player bound elsewhere stays there");
    write(file, "B=Mouse:Left\n");
    check(Remap::load(file, &loaded, &message) && loaded.inputs(Action::B).size() == 1,
          "a customised B line is left alone");

    // A partial hand-written file: the actions it names, defaults for the rest.
    write(file, "# mine\nShake=Key:G\n");
    check(Remap::load(file, &loaded, &message) && slot(loaded, Action::Shake, 0) == kG &&
              has(loaded, Action::A, kSpace),
          "a partial file overrides only its actions");

    // Invalid: defaults, a message, and the file kept as .bad when first replaced.
    write(file, "Shake=Key:NoSuchKey\n");
    check(!Remap::load(file, &loaded, &message) && loaded.serialize() == In::Bindings::defaults().serialize(),
          "an invalid file gives the defaults");
    check(message.find("controls.txt") != std::string::npos && message.find("default") != std::string::npos &&
              Remap::loadMessage() == message,
          "with a message: " + message);
    check(read(file) == "Shake=Key:NoSuchKey\n", "loading leaves the invalid file alone");
    check(Remap::save(loaded, &error) && read(dir / "controls.txt.bad") == "Shake=Key:NoSuchKey\n",
          "the first save keeps the invalid file as controls.txt.bad");
    check(Remap::loadMessage().empty(), "the message clears once saved");
    write(file, "Garbage without an equals sign\n");
    check(!Remap::load(file, &loaded, &message), "a line without = is invalid");

    // Home without a keyboard input gets its default keys back.
    write(file, "Home=Pad:Guide\n");
    check(Remap::load(file, &loaded, &message) && has(loaded, Action::Home, kF1) && has(loaded, Action::Home, kGrave) &&
              has(loaded, Action::Home, Binding::pad(In::PadButton::Guide)) && !message.empty(),
          "a file that unbinds Home gets F1 and ` back, with a message");
    write(file, "Home=\n");
    check(Remap::load(file, &loaded, &message) && Remap::homeReachable(loaded), "an empty Home line is repaired too");

    // Saving into a directory that is gone fails with a message, not a crash.
    Remap::load(dir / "gone" / "controls.txt", &loaded, &message);
    check(!Remap::save(loaded, &error) && !error.empty(), "an unwritable file fails with a message");

    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    testSlots();
    testAssign();
    testHomeGuard();
    testReset();
    testCapture();
    testMenuScroll();
    testFile();
    if (gFailures > 0) {
        std::fprintf(stderr, "%d of %d remap checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("input remap tests passed (%d checks)\n", gChecks);
    return 0;
}
