// Tests for the native HOME Button Menu replacement: the menu's lifecycle,
// selections, cancellation, black-out, and focus/repeat rules; the HBM bridge
// reading the game's KPAD data; the game's own HomeButtonMenuWrapper.cpp
// calling the menu through RSO::HBM*; and the Dear ImGui overlay's output.

#include <revolution/hbm.h>
#include <revolution/kpad.h>
#include <revolution/wpad.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Game/System/HomeButtonMenuWrapper.hpp"
#include "petari/home_menu.hpp"
#include "petari/progress.hpp"
#include "petari/launch_stage.hpp"
#include "petari/home_menu_hbm.hpp"

#ifdef PETARI_HOME_MENU_TEST_IMGUI
#include <imgui.h>
#endif

namespace HM = PetariNative::HomeMenu;
using HM::Button::A;
using HM::Button::B;
using HM::Button::Down;
using HM::Button::Home;
using HM::Button::Left;
using HM::Button::Right;
using HM::Button::Plus;
using HM::Button::Up;

namespace {

int checks = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
        std::exit(1);
    }
}

struct SoundEvent {
    int event;
    int num;
};
std::vector<SoundEvent> sounds;

int recordSound(int event, int num) {
    sounds.push_back({event, num});
    return event == HM::Sound::EventPlaySound ? 1 : 0;
}

bool played(int num) {
    for (const SoundEvent& s : sounds) {
        if (s.event == HM::Sound::EventPlaySound && s.num == num) {
            return true;
        }
    }
    return false;
}

bool sent(int event) {
    for (const SoundEvent& s : sounds) {
        if (s.event == event) {
            return true;
        }
    }
    return false;
}

int playedCount(int num) {
    int count = 0;
    for (const SoundEvent& s : sounds) {
        if (s.event == HM::Sound::EventPlaySound && s.num == num) {
            count++;
        }
    }
    return count;
}

HM::FrameInput none() {
    return {};
}

HM::FrameInput press(std::uint32_t buttons) {
    HM::FrameInput input;
    input.hold = buttons;
    input.trigger = buttons;
    return input;
}

HM::FrameInput hold(std::uint32_t buttons) {
    HM::FrameInput input;
    input.hold = buttons;
    return input;
}

HM::FrameInput pointer(float x, float y, std::uint32_t trigger = 0) {
    HM::FrameInput input;
    input.pointerValid = true;
    input.pointerX = x;
    input.pointerY = y;
    input.hold = trigger;
    input.trigger = trigger;
    return input;
}

void center(const HM::Rect& r, float* x, float* y) {
    *x = (r.x0 + r.x1) * 0.5f;
    *y = (r.y0 + r.y1) * 0.5f;
}

HM::Menu newMenu(float frameDelta = 1.0f) {
    HM::Menu menu;
    HM::Config config;
    config.sound = recordSound;
    config.frameDelta = frameDelta;
    menu.create(config);
    sounds.clear();
    return menu;
}

// Opens the menu and runs the opening fade.
void openToList(HM::Menu& menu) {
    menu.open();
    for (int i = 0; i < 10; i++) {
        menu.update(none());
    }
    check(menu.phase() == HM::Phase::List, "opening fade takes 10 frames");
}

int framesUntilSelection(HM::Menu& menu, int limit = 100) {
    for (int i = 1; i <= limit; i++) {
        menu.update(none());
        if (menu.selection() != HM::Selection::None) {
            return i;
        }
    }
    return -1;
}

void testLifecycle() {
    HM::Menu menu = newMenu();
    check(menu.selection() == HM::Selection::None, "no selection before opening");
    check(!menu.view().visible, "hidden before opening");
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Closed, "update before opening does nothing");

    menu.open();
    check(menu.phase() == HM::Phase::Opening, "open starts the fade in");
    check(played(HM::Sound::HomeButton), "open plays the HOME button sound");
    check(menu.view().visible && menu.view().panelOpacity == 0.0f, "visible at zero opacity on the first frame");
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Opening && menu.focus() == 0, "input ignored while opening");
    for (int i = 0; i < 4; i++) {
        menu.update(none());
    }
    check(std::fabs(menu.view().panelOpacity - 0.5f) < 1e-5f, "panel half faded in after 5 frames");
    check(std::fabs(menu.view().dimOpacity - 0.25f) < 1e-5f, "dim follows the panel");
    for (int i = 0; i < 5; i++) {
        menu.update(none());
    }
    check(menu.phase() == HM::Phase::List, "list after the fade in");
    const HM::View list = menu.view();
    check(list.itemCount == 6 && std::strcmp(list.items[0].label, "Resume") == 0 &&
              std::strcmp(list.items[1].label, "Controls") == 0 && std::strcmp(list.items[2].label, "Mods") == 0 &&
              std::strcmp(list.items[3].label, "My Progress") == 0 && std::strcmp(list.items[4].label, "Restart from Title") == 0 &&
              std::strcmp(list.items[5].label, "Quit") == 0,
          "list offers Resume, Controls, Mods, My Progress, Restart from Title, Quit");
    check(list.bands && list.panel.y0 >= -1.0f && list.messageY < list.panel.y1 && list.items[0].rect.y0 > list.panel.y1 &&
              list.items[5].rect.y1 < 0.9f,
          "the list: title and message in the top band, six items between the bands");
    check(list.items[0].focused && !list.items[1].focused, "Resume focused first");

    for (int i = 0; i < 300; i++) {
        menu.update(none());
    }
    check(menu.selection() == HM::Selection::None, "menu waits for the player");

    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Closing, "Resume closes");
    check(played(HM::Sound::ReturnApp) && sent(HM::Sound::EventBeginExitAnim), "resume sound and exit event");
    check(framesUntilSelection(menu) == 10, "resume reported after the 10-frame fade out");
    check(menu.selection() == HM::Selection::Resume, "resume result");
    check(sent(HM::Sound::EventEndMenu), "end-of-menu event");
    check(!menu.view().visible, "hidden once finished");
    menu.update(press(A));
    check(menu.selection() == HM::Selection::Resume, "result stable until reopened");

    menu.open();
    check(menu.selection() == HM::Selection::None && menu.phase() == HM::Phase::Opening && menu.focus() == 0,
          "reopening starts fresh");
}

void testSelections() {
    for (int quit = 0; quit < 2; quit++) {
        HM::Menu menu = newMenu();
        openToList(menu);
        menu.update(press(Down));
        menu.update(press(Down));
        menu.update(press(Down));
        menu.update(press(Down));
        if (quit) {
            menu.update(press(Down));
        }
        sounds.clear();
        menu.update(press(A));
        check(menu.phase() == HM::Phase::Confirm, "Restart and Quit ask first");
        check(played(HM::Sound::Select), "select sound");
        check(menu.confirming() == (quit ? HM::Selection::Quit : HM::Selection::Restart), "confirming the chosen action");
        const HM::View confirm = menu.view();
        check(confirm.itemCount == 2 && std::strcmp(confirm.items[1].label, "Cancel") == 0, "confirm offers Cancel");
        check(std::strcmp(confirm.items[0].label, quit ? "Quit" : "Restart") == 0, "confirm names the action");
        check(menu.focus() == 1, "Cancel focused by default");
        menu.update(press(A));
        check(menu.phase() == HM::Phase::List && menu.focus() == (quit ? 5 : 4), "A on Cancel returns to the item");

        menu.update(press(A));
        menu.update(press(Up));
        check(menu.focus() == 0, "Up to the action");
        sounds.clear();
        menu.update(press(A));
        check(menu.phase() == HM::Phase::BlackOut, "accepting fades to black");
        check(played(quit ? HM::Sound::GotoMenu : HM::Sound::ResetApp), "leave sound");
        check(sent(HM::Sound::EventBeginBlackOut), "black-out event");
        for (int i = 0; i < 15; i++) {
            menu.update(none());
        }
        check(std::fabs(menu.view().blackOpacity - 0.5f) < 1e-5f, "half black after 15 frames");
        check(menu.view().panelOpacity == 1.0f, "panel stays under the black");
        check(menu.selection() == HM::Selection::None, "no result before full black");
        check(framesUntilSelection(menu) == 15, "result at full black after 30 frames");
        check(menu.selection() == (quit ? HM::Selection::Quit : HM::Selection::Restart), "leave result");
    }
}

void testCancellation() {
    HM::Menu menu = newMenu();
    openToList(menu);
    menu.update(press(Plus | B));  // Escape
    check(menu.phase() == HM::Phase::Closing, "Escape in the list resumes");
    check(framesUntilSelection(menu) == 10 && menu.selection() == HM::Selection::Resume, "Escape resume result");

    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    sounds.clear();
    menu.update(press(Plus | B));
    check(menu.phase() == HM::Phase::List && menu.focus() == 4, "Escape in a confirmation goes back");
    check(played(HM::Sound::Cancel), "cancel sound");

    menu.update(press(A));
    sounds.clear();
    menu.update(press(Home));
    check(menu.phase() == HM::Phase::Closing, "Home closes from a confirmation");
    check(played(HM::Sound::ReturnApp), "Home plays the return sound");
    check(framesUntilSelection(menu) == 10 && menu.selection() == HM::Selection::Resume, "Home resumes");

    openToList(menu);
    menu.update(press(B));
    check(menu.phase() == HM::Phase::Closing, "B without the pointer on an item is back");

    openToList(menu);
    menu.update(pointer(0.95f, 0.95f, B));
    check(menu.phase() == HM::Phase::Closing, "a left click outside the items is back");

    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Confirm, "at a confirmation");
    menu.update(pointer(0.95f, 0.95f, B));
    check(menu.phase() == HM::Phase::List, "a left click outside a confirmation cancels it");

    // Held Home from the frame that opened the menu does not close it.
    openToList(menu);
    menu.update(hold(Home));
    check(menu.phase() == HM::Phase::List, "only a new Home press closes");
    // Input during the closing fade is ignored.
    menu.update(press(Home));
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Closing, "input ignored while closing");
}

void testBlackOut() {
    HM::Menu menu = newMenu();
    menu.startBlackOut();
    check(menu.phase() == HM::Phase::Closed && menu.selection() == HM::Selection::None,
          "black-out while never opened does nothing");

    openToList(menu);
    menu.update(press(A));
    framesUntilSelection(menu);
    menu.startBlackOut();
    check(menu.selection() == HM::Selection::Resume, "black-out after the menu closed does nothing");

    for (HM::Phase at : {HM::Phase::Opening, HM::Phase::List, HM::Phase::Confirm, HM::Phase::Closing}) {
        sounds.clear();
        menu.open();
        if (at != HM::Phase::Opening) {
            for (int i = 0; i < 10; i++) {
                menu.update(none());
            }
        }
        if (at == HM::Phase::Confirm) {
            menu.update(press(Down));
            menu.update(press(Down));
            menu.update(press(Down));
            menu.update(press(Down));
            menu.update(press(A));
        } else if (at == HM::Phase::Closing) {
            menu.update(press(A));
        }
        check(menu.phase() == at, "reached the phase to interrupt");
        menu.startBlackOut();
        check(menu.phase() == HM::Phase::BlackOut, "console reset starts the black-out");
        check(sent(HM::Sound::EventBeginBlackOut), "black-out event on reset");
        menu.update(press(Home));
        menu.update(press(Plus));
        check(menu.phase() == HM::Phase::BlackOut, "no input during the black-out");
        check(framesUntilSelection(menu) == 28, "black-out completes 30 frames after it began");
        check(menu.selection() == HM::Selection::Restart, "black-out reports the reset result");
    }

    // A reset during a Quit fade keeps its timing and becomes the reset result.
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    menu.update(press(Up));
    menu.update(press(A));
    for (int i = 0; i < 20; i++) {
        menu.update(none());
    }
    const float black = menu.view().blackOpacity;
    menu.startBlackOut();
    check(menu.view().blackOpacity == black, "black-out continues from the current opacity");
    check(framesUntilSelection(menu) == 10, "black-out timing unchanged");
    check(menu.selection() == HM::Selection::Restart, "reset replaces the quit result");
}

void testFocusAndRepeat() {
    HM::Menu menu = newMenu();
    openToList(menu);
    sounds.clear();
    menu.update(press(Up));
    check(menu.focus() == 0 && sounds.empty(), "focus stops at the top, silently");
    menu.update(press(Down));
    check(menu.focus() == 1 && playedCount(HM::Sound::Focus) == 1, "Down moves the focus with a sound");
    menu.update(hold(Down));
    menu.update(press(Up | Down));
    check(menu.focus() == 1, "Up and Down together do nothing");

    // Repeat: the press moves once, holding repeats after 24 frames, then
    // every 6. A fresh list to have room to move.
    openToList(menu);
    menu.update(press(Down));
    check(menu.focus() == 1, "first move on the press");
    for (int i = 0; i < 23; i++) {
        menu.update(hold(Down));
    }
    check(menu.focus() == 1, "no repeat before 24 frames");
    menu.update(hold(Down));
    check(menu.focus() == 2, "repeat at 24 frames");
    menu.update(hold(0));
    menu.update(press(Up));
    for (int i = 0; i < 24; i++) {
        menu.update(hold(Up));
    }
    check(menu.focus() == 0, "repeat upward");
    // Repeat interval, on the four-item list: Down press at 0, hold to 24 (1 -> 2 is the press+repeat).
    openToList(menu);
    menu.update(press(Down));
    for (int i = 0; i < 24; i++) {
        menu.update(hold(Down));
    }
    check(menu.focus() == 2, "press + first repeat");
    for (int i = 0; i < 5; i++) {
        menu.update(hold(Down));
    }
    check(menu.focus() == 2, "no second repeat before 6 more frames");
    menu.update(hold(Down));
    check(menu.focus() == 3, "second repeat 6 frames later");
    for (int i = 0; i < 6; i++) {
        menu.update(hold(Down));
    }
    check(menu.focus() == 4, "third repeat");
    for (int i = 0; i < 6; i++) {
        menu.update(hold(Down));
    }
    check(menu.focus() == 5, "fourth repeat reaches the last item");
    sounds.clear();
    for (int i = 0; i < 12; i++) {
        menu.update(hold(Down));
    }
    check(menu.focus() == 5 && sounds.empty(), "repeats stop at the end without sound");

    // Interval, counted on the Up side: from item 2, press Up at 0 -> 1, repeat at 24 -> 0.
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Up));
    for (int i = 0; i < 23; i++) {
        menu.update(hold(Up));
    }
    check(menu.focus() == 1, "Up repeat not yet");
    menu.update(hold(Up));
    check(menu.focus() == 0, "Up repeat at 24");

    // A key held into the menu does not start repeating.
    openToList(menu);
    for (int i = 0; i < 60; i++) {
        menu.update(hold(Down));
    }
    check(menu.focus() == 0, "no repeat without a press in the menu");

    // Frame delta scales timing.
    HM::Menu fast = newMenu(2.0f);
    fast.open();
    for (int i = 0; i < 5; i++) {
        fast.update(none());
    }
    check(fast.phase() == HM::Phase::List, "opening at frameDelta 2 takes 5 updates");
    fast.update(press(Down));
    for (int i = 0; i < 12; i++) {
        fast.update(hold(Down));
    }
    check(fast.focus() == 2, "repeat at frameDelta 2 after 12 updates");

    // Pointer focus: moving onto an item focuses it; a resting pointer does not
    // override the keyboard; A activates the focus; B activates the item under
    // the pointer.
    openToList(menu);
    const HM::View view = menu.view();
    float x, y;
    center(view.items[5].rect, &x, &y);
    sounds.clear();
    menu.update(pointer(x, y));
    check(menu.focus() == 5 && playedCount(HM::Sound::Focus) == 1, "pointer moving onto Quit focuses it");
    menu.update(pointer(x, y));
    check(playedCount(HM::Sound::Focus) == 1, "no sound while resting");
    HM::FrameInput up = pointer(x, y, Up);
    menu.update(up);
    check(menu.focus() == 4, "keyboard moves the focus under a resting pointer");
    menu.update(pointer(x, y));
    check(menu.focus() == 4, "resting pointer does not steal the focus back");
    menu.update(pointer(x + 0.01f, y));
    check(menu.focus() == 5, "moving the pointer takes the focus");
    menu.update(pointer(0.95f, 0.95f));
    check(menu.focus() == 5, "leaving the items keeps the focus");
    menu.update(pointer(x, y, B));
    check(menu.phase() == HM::Phase::Confirm && menu.confirming() == HM::Selection::Quit, "left click on Quit");

    openToList(menu);
    center(menu.view().items[4].rect, &x, &y);
    menu.update(pointer(x, y));
    menu.update(pointer(0.95f, 0.95f));
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Confirm && menu.confirming() == HM::Selection::Restart, "A activates the focus");

    // The pointer resting where it was during the opening fade does not focus.
    menu = newMenu();
    menu.open();
    center(menu.view().items[2].rect, &x, &y);
    for (int i = 0; i < 10; i++) {
        menu.update(pointer(x, y));
    }
    menu.update(pointer(x, y));
    check(menu.phase() == HM::Phase::List && menu.focus() == 0, "resting pointer from before the list does not focus");

    // Widescreen keeps the panel's shape.
    menu = newMenu();
    openToList(menu);
    const float narrow = menu.view().items[0].rect.x1;
    menu.setWidescreen(true);
    check(std::fabs(menu.view().items[0].rect.x1 - narrow * 608.0f / 832.0f) < 1e-5f, "16:9 narrows the buttons in pointer space");
    center(menu.view().items[0].rect, &x, &y);
    check(menu.view().items[0].rect.x1 < narrow, "items narrow too");
}

// Seventeen lines like the input layer's summary (a header, then the
// longest texts it writes in each column).
std::vector<HM::ControlsEntry> sampleControls(int count = 17) {
    std::vector<HM::ControlsEntry> entries(count);
    for (int i = 0; i < count; i++) {
        std::snprintf(entries[i].action, sizeof(entries[i].action), i == 0 ? "" : "Tilt the remote by hand");
        std::snprintf(entries[i].inputs, sizeof(entries[i].inputs),
                      i == 0 ? "Keyboard and mouse" : "Hold Space / Right mouse on the target");
        std::snprintf(entries[i].pad, sizeof(entries[i].pad), i == 0 ? "Controller" : "D-pad up (leave: D-pad down)");
    }
    return entries;
}

void testControlsPage() {
    HM::Menu menu = newMenu();
    const std::vector<HM::ControlsEntry> entries = sampleControls();
    menu.setControls(entries.data(), static_cast<int>(entries.size()));
    openToList(menu);
    menu.update(press(Down));
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Controls && played(HM::Sound::Select), "Controls opens its page");
    HM::View view = menu.view();
    check(std::strcmp(view.title, "Controls") == 0 && view.itemCount == 1 && std::strcmp(view.items[0].label, "Back") == 0 &&
              view.items[0].focused,
          "the page has a title and a focused Back");
    check(view.lineCount == 17 && std::strcmp(view.lines[3].action, entries[3].action) == 0 &&
              std::strcmp(view.lines[15].inputs, entries[15].inputs) == 0 &&
              std::strcmp(view.lines[16].pad, entries[16].pad) == 0,
          "the page shows the lines it was given, in order");
    check(view.panel.y0 < view.titleY && view.titleY < view.messageY && view.messageY < view.linesArea.y0 &&
              view.linesArea.y1 < view.items[0].rect.y0 && view.items[0].rect.y1 < view.panel.y1,
          "title, message, lines, and Back stack inside the panel");
    check(view.panel.y0 >= -1.0f && view.panel.y1 <= 1.0f && view.panel.x0 >= -1.0f && view.panel.x1 <= 1.0f,
          "the page fits the image");

    // Up and Down have nowhere to go; A, B, Plus and a click on Back return
    // to the list with Controls focused; Home closes.
    menu.update(press(Down));
    menu.update(press(Up));
    check(menu.phase() == HM::Phase::Controls && menu.focus() == 0, "Up and Down stay on Back");
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::List && menu.focus() == 1 && played(HM::Sound::Cancel), "A on Back returns");
    menu.update(press(A));
    menu.update(press(B));
    check(menu.phase() == HM::Phase::List && menu.focus() == 1, "B returns");
    menu.update(press(A));
    menu.update(press(Plus));
    check(menu.phase() == HM::Phase::List && menu.focus() == 1, "Escape returns");
    menu.update(press(A));
    float x, y;
    center(menu.view().items[0].rect, &x, &y);
    menu.update(pointer(x, y, B));
    check(menu.phase() == HM::Phase::List, "a click on Back returns");
    menu.update(press(A));
    menu.update(press(Home));
    check(menu.phase() == HM::Phase::Closing && framesUntilSelection(menu) == 10 &&
              menu.selection() == HM::Selection::Resume,
          "Home on the page resumes the game");

    // Kept across opens; a reset during the page blacks out.
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.view().lineCount == 17, "lines kept across opens");
    menu.startBlackOut();
    check(menu.phase() == HM::Phase::BlackOut && framesUntilSelection(menu) == 30 &&
              menu.selection() == HM::Selection::Restart,
          "console reset from the page");

    // At most kMaxControls lines; text always terminated.
    std::vector<HM::ControlsEntry> many = sampleControls(HM::kMaxControls + 5);
    std::memset(many[0].action, 'x', sizeof(many[0].action));
    std::memset(many[0].inputs, 'y', sizeof(many[0].inputs));
    std::memset(many[0].pad, 'z', sizeof(many[0].pad));
    HM::Menu full = newMenu();
    full.setControls(many.data(), static_cast<int>(many.size()));
    openToList(full);
    full.update(press(Down));
    full.update(press(A));
    const HM::View fullView = full.view();
    check(fullView.lineCount == HM::kMaxControls, "lines capped at kMaxControls");
    check(std::strlen(fullView.lines[0].action) == sizeof(fullView.lines[0].action) - 1 &&
              std::strlen(fullView.lines[0].inputs) == sizeof(fullView.lines[0].inputs) - 1 &&
              std::strlen(fullView.lines[0].pad) == sizeof(fullView.lines[0].pad) - 1,
          "unterminated text is cut, not overrun");
    full.setControls(nullptr, 0);
    check(full.view().lineCount == 0, "lines can be cleared");
}

void setPad(KPADStatus* status, u32 holdBits, u32 trigBits) {
    std::memset(status, 0, sizeof(*status));
    status->hold = holdBits;
    status->trig = trigBits;
}

// The Controls page as a remapper, on a fake backend that records what the
// page asks of it.
class FakeControls final : public HM::ControlsBackend {
public:
    int rows = 30;
    HM::CaptureStatus capture = HM::CaptureStatus::Waiting;
    HM::RemapOutcome placeOutcome = HM::RemapOutcome::Done;
    HM::RemapOutcome clearOutcome = HM::RemapOutcome::Done;
    int begins = 0, cancels = 0, clears = 0, resets = 0, resetAlls = 0;
    std::vector<HM::RemapChoice> choices;
    int lastRow = -1, lastSlot = -1;
    std::string noticeText;

    int rowCount() override { return rows; }
    void row(int index, HM::RemapRow* out) override {
        std::snprintf(out->label, sizeof(out->label), "Action %d", index);
        std::snprintf(out->slots[0], sizeof(out->slots[0]), "K%d", index);
    }
    void beginCapture() override { ++begins; capture = HM::CaptureStatus::Waiting; }
    void cancelCapture() override { ++cancels; }
    HM::CaptureStatus pollCapture(char* name, int size) override {
        std::snprintf(name, static_cast<size_t>(size), "F");
        return capture;
    }
    HM::RemapOutcome place(int row, int slot, HM::RemapChoice choice, char* other, int size) override {
        lastRow = row;
        lastSlot = slot;
        choices.push_back(choice);
        std::snprintf(other, static_cast<size_t>(size), "Spin");
        return choice == HM::RemapChoice::Check ? placeOutcome : HM::RemapOutcome::Done;
    }
    HM::RemapOutcome clearSlot(int row, int slot) override {
        ++clears;
        lastRow = row;
        lastSlot = slot;
        return clearOutcome;
    }
    HM::RemapOutcome resetRow(int row) override {
        ++resets;
        lastRow = row;
        return HM::RemapOutcome::Done;
    }
    HM::RemapOutcome resetAll() override {
        ++resetAlls;
        return HM::RemapOutcome::Done;
    }
    void notice(char* out, int size) override { std::snprintf(out, static_cast<size_t>(size), "%s", noticeText.c_str()); }
};

void openRemapper(HM::Menu& menu) {
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Controls, "Controls opens");
}

bool contains(const char* text, const char* part) {
    return std::strstr(text, part) != nullptr;
}

void testRemapper() {
    FakeControls fake;
    HM::Menu menu = newMenu();
    menu.setControlsBackend(&fake);
    fake.noticeText = "controls.txt has an error";
    openRemapper(menu);
    HM::View view = menu.view();
    check(view.remap.active && view.remap.totalRows == 30 && view.remap.rowCount == HM::kRemapVisibleRows,
          "the remapper shows the first 11 of 30 rows");
    check(contains(view.message, "controls.txt has an error"), "a load notice shows when the page opens");
    check(view.itemCount == 4 && std::strcmp(view.items[3].label, "Back") == 0, "Clear, Reset action, Reset all, Back");
    check(view.remap.focusRow == 0 && view.remap.focusSlot == 0 && !view.items[0].focused, "focus on the first slot");
    fake.noticeText.clear();

    // Navigation: Right moves along the slots (clamped), Down scrolls.
    for (int i = 0; i < 5; i++) menu.update(press(Right));
    check(menu.view().remap.focusSlot == HM::kRemapSlots - 1, "Right stops at the last slot");
    menu.update(press(Left));
    for (int i = 0; i < 12; i++) menu.update(press(Down));
    view = menu.view();
    check(view.remap.firstRow == 2 && view.remap.focusRow == 10 && view.remap.focusSlot == 2,
          "Down past the shown rows scrolls (row 12, slot 2)");
    for (int i = 0; i < 20; i++) menu.update(press(Down));
    view = menu.view();
    check(view.remap.focusRow == -1 && view.items[2].focused, "past the last row: the buttons, under the slot");
    check(view.remap.firstRow + view.remap.rowCount == 30, "scrolled to the end");
    menu.update(press(Up));
    check(menu.view().remap.focusRow == HM::kRemapVisibleRows - 1, "Up returns to the last row");

    // Capture: A starts it; the menu ignores its own buttons while waiting.
    menu.update(press(A));
    check(fake.begins == 1 && menu.view().remap.capturing, "A on a slot starts a capture");
    menu.update(press(Home | Plus | Down));
    check(menu.phase() == HM::Phase::Controls && menu.view().remap.capturing, "Home and Plus do nothing while capturing");
    fake.capture = HM::CaptureStatus::Captured;
    fake.placeOutcome = HM::RemapOutcome::Done;
    menu.update(none());
    view = menu.view();
    check(!view.remap.capturing && fake.choices.size() == 1 && fake.choices[0] == HM::RemapChoice::Check &&
              fake.lastRow == 29 && fake.lastSlot == 2,
          "the captured input goes to the focused slot");
    check(contains(view.message, "Action 29: F"), "the message says what changed");

    // A conflict: Swap / Keep both / Cancel, Cancel first.
    fake.choices.clear();
    fake.placeOutcome = HM::RemapOutcome::Conflict;
    menu.update(press(A));
    fake.capture = HM::CaptureStatus::Captured;
    menu.update(none());
    view = menu.view();
    check(view.itemCount == 3 && std::strcmp(view.items[0].label, "Swap") == 0 && view.items[2].focused,
          "a conflict offers Swap, Keep both and Cancel, on Cancel");
    check(contains(view.message, "F is already Spin"), "the conflict names the other action");
    menu.update(press(A));
    check(fake.choices.size() == 1 && menu.view().itemCount == 4, "Cancel changes nothing");
    for (int choice = 0; choice < 2; choice++) {
        fake.choices.clear();
        menu.update(press(Up));  // back from the buttons to the row (Cancel left the focus there)
        menu.update(press(A));
        fake.capture = HM::CaptureStatus::Captured;
        menu.update(none());
        menu.update(press(Left));
        menu.update(press(Left));
        if (choice == 1) menu.update(press(Right));
        menu.update(press(A));
        check(fake.choices.size() == 2 && fake.choices[1] == (choice == 0 ? HM::RemapChoice::Swap : HM::RemapChoice::KeepBoth),
              choice == 0 ? "Swap" : "Keep both");
    }
    fake.choices.clear();
    menu.update(press(A));
    fake.capture = HM::CaptureStatus::Captured;
    menu.update(none());
    menu.update(press(Plus));
    check(fake.choices.size() == 1 && menu.phase() == HM::Phase::Controls && menu.view().itemCount == 4,
          "Plus cancels a conflict and stays on the page");

    // Escape (the backend reports Cancelled) and the timeout.
    menu.update(press(A));
    fake.capture = HM::CaptureStatus::Cancelled;
    menu.update(none());
    check(!menu.view().remap.capturing && contains(menu.view().message, "Not changed"), "a cancelled capture");
    const int cancels = fake.cancels;
    menu.update(press(A));
    for (int i = 0; i < 599; i++) menu.update(none());
    check(menu.view().remap.capturing, "still waiting after 599 frames");
    menu.update(none());
    check(!menu.view().remap.capturing && fake.cancels == cancels + 1, "10 s without a press cancels the capture");

    // Home refused.
    fake.placeOutcome = HM::RemapOutcome::HomeUnreachable;
    menu.update(press(A));
    fake.capture = HM::CaptureStatus::Captured;
    menu.update(none());
    check(contains(menu.view().message, "must keep"), "a refused change says why");

    // The buttons act on the selected slot; Reset all needs a second A.
    for (int i = 0; i < 20; i++) menu.update(press(Down));
    while (menu.view().items[0].focused == false) menu.update(press(Left));
    menu.update(press(A));
    check(fake.clears == 1 && fake.lastRow == 29 && fake.lastSlot == 2, "Clear slot acts on the selected slot");
    menu.update(press(Right));
    menu.update(press(A));
    check(fake.resets == 1 && fake.lastRow == 29, "Reset action");
    menu.update(press(Right));
    menu.update(press(A));
    check(fake.resetAlls == 0 && contains(menu.view().items[2].label, "again"), "Reset all asks first");
    menu.update(press(A));
    check(fake.resetAlls == 1, "Reset all on the second A");
    menu.update(press(A));
    menu.update(press(Left));
    menu.update(press(Right));
    menu.update(press(A));
    check(fake.resetAlls == 1, "moving away disarms Reset all");
    menu.update(press(Right));
    menu.update(press(A));
    check(menu.phase() == HM::Phase::List && menu.focus() == 1, "Back returns to the list on Controls");

    // The pointer: moving onto a slot focuses it; B (left click) there captures.
    menu.update(press(A));
    view = menu.view();
    float x = 0.0f, y = 0.0f;
    center(view.remap.cells[3][1], &x, &y);
    menu.update(pointer(x, y));
    check(menu.view().remap.focusRow == 3 && menu.view().remap.focusSlot == 1, "pointing at a slot focuses it");
    const int begins = fake.begins;
    menu.update(pointer(x, y, B));
    check(fake.begins == begins + 1 && menu.view().remap.capturing, "clicking a slot captures");
    // Closing the menu while capturing cancels the capture.
    const int before = fake.cancels;
    menu.startBlackOut();
    check(fake.cancels == before + 1, "a black-out cancels the capture");

    // Without a backend the page is the read-only summary.
    HM::Menu summary = newMenu();
    openRemapper(summary);
    check(!summary.view().remap.active && summary.view().itemCount == 1, "no backend: the summary page");
}

// Scrolling the remapper: wheel rows, pages (Page Up/Down, the controller's
// LB/LT), and the scrollbar with the pointer (arrows, track, dragging the thumb).
void testRemapperScrolling() {
    FakeControls fake;
    fake.rows = 34;
    HM::Menu menu = newMenu();
    menu.setControlsBackend(&fake);
    openRemapper(menu);

    HM::FrameInput wheel;
    wheel.scrollRows = 3;
    menu.update(wheel);
    HM::View view = menu.view();
    check(view.remap.firstRow == 3 && view.remap.focusRow == 0, "the wheel scrolls; the focus stays on a shown row (row 3)");
    wheel.scrollRows = 100;
    menu.update(wheel);
    check(menu.view().remap.firstRow == 34 - HM::kRemapVisibleRows, "the wheel stops at the end");
    wheel.scrollRows = -100;
    menu.update(wheel);
    view = menu.view();
    check(view.remap.firstRow == 0 && view.remap.focusRow + view.remap.firstRow == HM::kRemapVisibleRows - 1,
          "back to the top; the focus kept to the last shown row");

    HM::FrameInput page;
    page.scrollPages = 1;
    menu.update(page);
    view = menu.view();
    check(view.remap.firstRow == HM::kRemapVisibleRows && view.remap.firstRow + view.remap.focusRow == 2 * HM::kRemapVisibleRows - 1,
          "Page Down moves the list and the focus a page");
    menu.update(press(HM::Button::C));
    check(menu.view().remap.firstRow == 0, "the controller's LB (Nunchuk C) pages up");
    menu.update(press(HM::Button::Z));
    check(menu.view().remap.firstRow == HM::kRemapVisibleRows, "LT (Nunchuk Z) pages down");
    page.scrollPages = -5;
    menu.update(page);
    check(menu.view().remap.firstRow == 0 && menu.view().remap.focusRow == 0, "Page Up stops at the top");

    // The scrollbar: the thumb is the shown fraction; arrows step a row; the track pages.
    view = menu.view();
    const HM::Rect track = view.remap.track;
    const float thumbShare = (view.remap.thumb.y1 - view.remap.thumb.y0) / (track.y1 - track.y0);
    check(std::fabs(thumbShare - 11.0f / 34.0f) < 0.01f && view.remap.thumb.y0 == track.y0, "the thumb: 11 of 34, at the top");
    float x = 0.0f, y = 0.0f;
    center(view.remap.downArrow, &x, &y);
    menu.update(pointer(x, y));
    menu.update(pointer(x, y, B));
    check(menu.view().remap.firstRow == 1 && menu.phase() == HM::Phase::Controls, "the down arrow scrolls one row");
    center(view.remap.upArrow, &x, &y);
    menu.update(pointer(x, y, B));
    check(menu.view().remap.firstRow == 0, "the up arrow scrolls back");
    x = (track.x0 + track.x1) * 0.5f;
    menu.update(pointer(x, track.y1 - 0.01f, B));
    check(menu.view().remap.firstRow == HM::kRemapVisibleRows, "a click on the track below the thumb pages down");
    menu.update(pointer(x, track.y0 + 0.01f, B));
    check(menu.view().remap.firstRow == 0, "above the thumb pages up");

    // Drag the thumb to the bottom and back to the middle.
    view = menu.view();
    const float grabY = (view.remap.thumb.y0 + view.remap.thumb.y1) * 0.5f;
    menu.update(pointer(x, grabY, B));
    check(menu.view().remap.dragging, "pressing on the thumb grabs it");
    HM::FrameInput drag = pointer(x, track.y1 + 0.5f);
    drag.hold = B;
    menu.update(drag);
    check(menu.view().remap.firstRow == 34 - HM::kRemapVisibleRows && menu.view().remap.dragging,
          "dragging past the end shows the last rows");
    drag.pointerY = grabY + ((track.y1 - track.y0) - (view.remap.thumb.y1 - view.remap.thumb.y0)) * 0.5f;
    menu.update(drag);
    const int middle = menu.view().remap.firstRow;
    check(middle >= 10 && middle <= 13, "dragging to the middle (" + std::to_string(middle) + ")");
    menu.update(pointer(x, drag.pointerY));
    check(!menu.view().remap.dragging && menu.view().remap.firstRow == middle, "releasing drops the thumb where it is");
    check(menu.phase() == HM::Phase::Controls && fake.begins == 0, "scrolling never captured or left the page");

    // A click on the panel's empty space is not "back"; outside the panel it is.
    view = menu.view();
    menu.update(pointer(0.0f, (view.panel.y0 + view.titleY) * 0.5f, B));
    check(menu.phase() == HM::Phase::Controls, "a click inside the panel stays");
    menu.update(pointer(view.panel.x1 + 0.05f, 0.0f, B));
    check(menu.phase() == HM::Phase::List, "a click outside the panel goes back");

    // No scrolling while capturing.
    openRemapper(menu);
    menu.update(press(A));
    wheel.scrollRows = 5;
    menu.update(wheel);
    check(menu.view().remap.firstRow == 0 && menu.view().remap.capturing, "the wheel does nothing during a capture");
}

void testBridgeInput() {
    HBMControllerData data;
    KPADStatus pads[WPAD_MAX_CONTROLLERS];
    for (int i = 0; i < WPAD_MAX_CONTROLLERS; i++) {
        data.wiiCon[i].kpad = nullptr;
        data.wiiCon[i].use_devtype = WPAD_DEV_CORE;
        data.wiiCon[i].pos.x = data.wiiCon[i].pos.y = 0.0f;
        setPad(&pads[i], 0, 0);
    }
    HM::FrameInput input = HM::frameInput(&data);
    check(input.hold == 0 && input.trigger == 0 && !input.pointerValid, "no controllers, no input");
    check(HM::frameInput(nullptr).hold == 0, "null controller data");

    data.wiiCon[0].kpad = &pads[0];
    setPad(&pads[0], WPAD_BUTTON_A | WPAD_BUTTON_PLUS | WPAD_BUTTON_1, WPAD_BUTTON_A);
    data.wiiCon[1].kpad = &pads[1];
    setPad(&pads[1], WPAD_BUTTON_HOME | WPAD_BUTTON_B | WPAD_BUTTON_UP | WPAD_BUTTON_DOWN, WPAD_BUTTON_HOME);
    input = HM::frameInput(&data);
    check(input.hold == (A | Plus | Home | B | Up | Down), "buttons of every controller combined");
    check(input.trigger == (A | Home), "triggers combined");

    // Pointer from the first controller aiming at the screen.
    pads[0].dpd_valid_fg = 0;
    pads[1].dpd_valid_fg = 2;
    data.wiiCon[0].pos = {0.9f, 0.9f};
    data.wiiCon[1].pos = {-0.25f, 0.5f};
    input = HM::frameInput(&data);
    check(input.pointerValid && input.pointerX == -0.25f && input.pointerY == 0.5f, "pointer from a valid controller");

    // Nunchuk stick: W/S (up is positive) with an edge per channel.
    data.wiiCon[1].kpad = nullptr;
    setPad(&pads[0], 0, 0);
    data.wiiCon[0].use_devtype = WPAD_DEV_FREESTYLE;
    pads[0].ex_status.fs.stick.y = 1.0f;
    input = HM::frameInput(&data);
    check(input.hold == Up && input.trigger == Up, "stick up is a press");
    input = HM::frameInput(&data);
    check(input.hold == Up && input.trigger == 0, "stick held is a hold");
    pads[0].ex_status.fs.stick.y = -1.0f;
    input = HM::frameInput(&data);
    check(input.hold == Down && input.trigger == Down, "stick down");
    pads[0].ex_status.fs.stick.y = 0.3f;
    input = HM::frameInput(&data);
    check(input.hold == 0, "stick inside the dead zone");
    data.wiiCon[0].use_devtype = WPAD_DEV_CORE;
    pads[0].ex_status.fs.stick.y = 1.0f;
    check(HM::frameInput(&data).hold == 0, "no stick without a Nunchuk");
}

// The game's wrapper, driven as HomeButtonLayout drives it.
void testWrapper() {
    RSO::setupRsoHomeButtonMenu();

    HBMDataInfo info;
    std::memset(&info, 0, sizeof(info));
    info.sound_callback = recordSound;
    info.frameDelta = 1.0f;
    info.adjust.x = 832.0f / 608.0f;
    info.adjust.y = 1.0f;
    RSO::HBMCreate(&info);
    RSO::HBMSetAdjustFlag(1);
    check(!HM::publishedView().visible, "nothing published after create");
    check(RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL, "no selection after create");
    RSO::HBMStartBlackOut();  // forceToDeactive with the menu closed
    check(RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL, "black-out while closed is ignored");

    HBMControllerData data;
    KPADStatus pad;
    for (int i = 0; i < WPAD_MAX_CONTROLLERS; i++) {
        data.wiiCon[i].kpad = nullptr;
        data.wiiCon[i].use_devtype = WPAD_DEV_FREESTYLE;
        data.wiiCon[i].pos.x = data.wiiCon[i].pos.y = 0.0f;
    }
    data.wiiCon[0].kpad = &pad;

    // exeActive's first step, then one HBMCalc per frame; draw() in between.
    sounds.clear();
    RSO::HBMInit();
    RSO::HBMSetAdjustFlag(1);
    check(played(HM::Sound::HomeButton), "HBMInit plays through the game's callback");
    int frames = 0;
    auto frame = [&](u32 holdBits, u32 trigBits) {
        setPad(&pad, holdBits, trigBits);
        RSO::HBMDraw();
        RSO::HBMCalc(&data);
        frames++;
    };
    frame(WPAD_BUTTON_HOME, 0);
    check(HM::publishedView().visible, "HBMDraw publishes the view");
    check(HM::publishedView().items[0].rect.x1 < 0.33f, "adjust flag applied");
    for (int i = 0; i < 12; i++) {
        frame(0, 0);
    }
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_A, WPAD_BUTTON_A);
    check(HM::instance().phase() == HM::Phase::Confirm, "Restart chosen through RSO::HBMCalc");
    frame(WPAD_BUTTON_UP, WPAD_BUTTON_UP);
    frame(WPAD_BUTTON_A, WPAD_BUTTON_A);
    while (RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL && frames < 200) {
        frame(0, 0);
        if (RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL) {
            check(HM::publishedView().visible, "view published while running");
        }
    }
    check(RSO::HBMGetSelectBtnNum() == HBM_SELECT_BTN2, "Restart reports HBM_SELECT_BTN2");
    check(!HM::publishedView().visible, "view hidden once a selection is reported");

    // Console reset while open: the black-out reports BTN2.
    RSO::HBMInit();
    frame(0, 0);
    RSO::HBMStartBlackOut();
    frames = 0;
    while (RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL && frames < 200) {
        frame(0, 0);
    }
    check(RSO::HBMGetSelectBtnNum() == HBM_SELECT_BTN2 && frames == 30, "forced black-out reports BTN2");

    // Quit reports BTN1; Home reports HOMEBTN.
    RSO::HBMInit();
    for (int i = 0; i < 10; i++) {
        frame(0, 0);
    }
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN);
    frame(WPAD_BUTTON_A, WPAD_BUTTON_A);
    frame(WPAD_BUTTON_UP, WPAD_BUTTON_UP);
    frame(WPAD_BUTTON_A, WPAD_BUTTON_A);
    frames = 0;
    while (RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL && frames < 200) {
        frame(0, 0);
    }
    check(RSO::HBMGetSelectBtnNum() == HBM_SELECT_BTN1, "Quit reports HBM_SELECT_BTN1");

    RSO::HBMInit();
    for (int i = 0; i < 10; i++) {
        frame(0, 0);
    }
    frame(WPAD_BUTTON_HOME, WPAD_BUTTON_HOME);
    frames = 0;
    while (RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL && frames < 200) {
        frame(0, 0);
    }
    check(RSO::HBMGetSelectBtnNum() == HBM_SELECT_HOMEBTN && frames == 10, "Home reports HBM_SELECT_HOMEBTN");
}

#ifdef PETARI_HOME_MENU_TEST_IMGUI
struct Bounds {
    int vertices = 0;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    float blackAlpha = 0.0f;  // most opaque pure-black vertex
    float dimAlpha = 0.0f;    // most opaque vertex of the pause layout's teal bands
    bool focusColor = false;
};

Bounds drawOverlay(float x, float y, float w, float h) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    HM::drawImGuiOverlay(x, y, w, h);
    ImGui::Render();
    Bounds bounds;
    const ImDrawData* data = ImGui::GetDrawData();
    for (int i = 0; i < data->CmdListsCount; i++) {
        const ImDrawList* list = data->CmdLists[i];
        for (const ImDrawVert& v : list->VtxBuffer) {
            bounds.vertices++;
            bounds.x0 = std::fmin(bounds.x0, v.pos.x);
            bounds.y0 = std::fmin(bounds.y0, v.pos.y);
            bounds.x1 = std::fmax(bounds.x1, v.pos.x);
            bounds.y1 = std::fmax(bounds.y1, v.pos.y);
            const ImVec4 c = ImGui::ColorConvertU32ToFloat4(v.col);
            if (c.x == 0.0f && c.y == 0.0f && c.z == 0.0f) {
                bounds.blackAlpha = std::fmax(bounds.blackAlpha, c.w);
            }
            if (std::fabs(c.x - 0.03f) < 0.01f && std::fabs(c.y - 0.43f) < 0.01f && std::fabs(c.z - 0.53f) < 0.01f) {
                bounds.dimAlpha = std::fmax(bounds.dimAlpha, c.w);
            }
            if (c.w > 0.99f && c.x > 0.9f && c.y > 0.7f && c.z < 0.3f) {
                bounds.focusColor = true;
            }
        }
    }
    return bounds;
}

// The hold ring drawn directly with a key name (drawImGuiOverlay passes the bound key when the
// input layer is built; this test build has none).
int ringVertices(float progress, const char* key) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    HM::drawHoldRing(progress, 160.0f, 40.0f, 960.0f, 720.0f, key);
    ImGui::Render();
    int count = 0;
    const ImDrawData* data = ImGui::GetDrawData();
    for (int i = 0; i < data->CmdListsCount; i++) count += data->CmdLists[i]->VtxBuffer.Size;
    return count;
}

int cueVertices(float mx, float my, float px, float py) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    HM::drawSteerCue(mx, my, px, py, 160.0f, 40.0f, 960.0f, 720.0f);
    ImGui::Render();
    int count = 0;
    const ImDrawData* data = ImGui::GetDrawData();
    for (int i = 0; i < data->CmdListsCount; i++) count += data->CmdLists[i]->VtxBuffer.Size;
    return count;
}

void testImGuiOverlay() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    HM::publish(HM::View{});
    check(drawOverlay(160.0f, 40.0f, 960.0f, 720.0f).vertices == 0, "hidden menu draws nothing");

    // The hold-to-skip ring: nothing without a source or at 0, a ring in the top right corner when
    // held, nothing while the menu itself is up.
    static float sHold = 0.0f;
    check(drawOverlay(160.0f, 40.0f, 960.0f, 720.0f).vertices == 0, "no hold source: no ring");
    HM::setHoldProgressSource([]() { return sHold; });
    check(drawOverlay(160.0f, 40.0f, 960.0f, 720.0f).vertices == 0, "hold progress 0: no ring");
    sHold = -1.0f;
    check(drawOverlay(160.0f, 40.0f, 960.0f, 720.0f).vertices == 0, "inactive (negative) hold: no ring");
    sHold = 0.5f;
    const Bounds half = drawOverlay(160.0f, 40.0f, 960.0f, 720.0f);
    check(half.vertices > 50 && half.x0 > 160.0f + 960.0f * 0.5f && half.y1 < 40.0f + 720.0f * 0.5f &&
              half.x1 <= 1120.0f + 0.5f && half.y0 >= 40.0f - 0.5f,
          "half-held ring sits in the image's top right corner");
    sHold = 1.0f;
    const Bounds fullRing = drawOverlay(160.0f, 40.0f, 960.0f, 720.0f);
    check(fullRing.vertices >= half.vertices, "a full ring draws at least the half ring's geometry");
    sHold = 7.0f;
    check(drawOverlay(160.0f, 40.0f, 960.0f, 720.0f).vertices == fullRing.vertices, "progress past 1 is clamped");
    // The Point-to-move steering cue: nothing inside Mario's dead zone or with bad numbers, an arrow
    // otherwise, and a source that says "inactive" draws nothing.
    HM::setHoldProgressSource(nullptr);  // the ring's test source must not draw in these frames
    check(cueVertices(0.0f, 0.0f, 0.0f, 0.0f) == 0, "pointer on Mario: no cue");
    check(cueVertices(0.0f, 0.0f, 0.03f, 0.0f) == 0, "pointer inside the dead zone: no cue");
    check(cueVertices(0.0f, 0.0f, 0.8f, -0.4f) >= 10, "pointer far away: an arrow is drawn");
    check(cueVertices(0.0f, 0.0f, std::nanf(""), 0.0f) == 0, "a NaN pointer draws nothing");
    check(cueVertices(0.0f, 0.0f, 0.8f, 0.0f) == cueVertices(0.0f, 0.0f, -0.8f, 0.0f), "the arrow has the same geometry in any direction");
    static bool sActive = false;
    HM::setSteerCueSource([](float* mx, float* my, float* px, float* py) {
        *mx = 0.0f; *my = 0.2f; *px = 0.7f; *py = -0.3f;
        return sActive;
    });
    HM::publish(HM::View{});
    check(drawOverlay(160.0f, 40.0f, 960.0f, 720.0f).vertices == 0, "cue source inactive: nothing drawn");
    sActive = true;
    const Bounds cue = drawOverlay(160.0f, 40.0f, 960.0f, 720.0f);
    check(cue.vertices >= 10 && cue.x0 >= 160.0f - 0.5f && cue.x1 <= 1120.0f + 0.5f && cue.y0 >= 40.0f - 0.5f && cue.y1 <= 760.0f + 0.5f,
          "active cue draws an arrow inside the image");
    HM::Menu cueMenu = newMenu();
    openToList(cueMenu);
    HM::publish(cueMenu.view());
    const int withMenu = drawOverlay(160.0f, 40.0f, 960.0f, 720.0f).vertices;
    sActive = false;
    check(withMenu == drawOverlay(160.0f, 40.0f, 960.0f, 720.0f).vertices, "the cue is hidden while the menu is open");
    HM::setSteerCueSource(nullptr);
    HM::publish(HM::View{});
    const int icon = ringVertices(0.5f, nullptr), spaceKey = ringVertices(0.5f, "Space"), shiftKey = ringVertices(0.5f, "Left Shift");
    check(icon > 50 && spaceKey > 50 && shiftKey > 50, "the ring draws with the A icon and with a key name");
    check(ringVertices(0.5f, "") == icon, "an empty key name falls back to the A icon");
    check(spaceKey != icon || shiftKey != spaceKey, "the key name is drawn in place of the icon");
    check(ringVertices(0.0f, "Space") == 0, "no ring at 0 even with a key name");
    HM::Menu menu = newMenu();
    openToList(menu);
    HM::publish(menu.view());
    const Bounds list = drawOverlay(160.0f, 40.0f, 960.0f, 720.0f);
    sHold = 0.0f;
    const Bounds listNoRing = drawOverlay(160.0f, 40.0f, 960.0f, 720.0f);
    check(list.vertices == listNoRing.vertices, "the ring is hidden while the menu is open");
    HM::setHoldProgressSource(nullptr);
    check(list.vertices > 100, "list draws the dim, panel, items, and text");
    check(list.x0 >= 160.0f - 0.5f && list.x1 <= 1120.0f + 0.5f && list.y0 >= 40.0f - 0.5f && list.y1 <= 760.0f + 0.5f,
          "drawing stays inside the game image");
    check(list.x0 < 161.0f && list.y1 > 759.0f, "dim covers the image");
    check(list.focusColor, "focused item highlighted");
    check(list.blackAlpha < 0.01f, "nothing is black while the list is shown");
    check(std::fabs(list.dimAlpha - 0.8f) < 0.01f, "teal bands top and bottom, as the game's pause screen has");
    check(!ImGui::GetIO().WantCaptureMouse && !ImGui::GetIO().WantCaptureKeyboard, "overlay takes no input");

    // Every confirmation's text fits the panel, in 4:3 and 16:9 images.
    for (int quit = 0; quit < 2; quit++) {
        for (int wide = 0; wide < 2; wide++) {
            HM::Menu confirm = newMenu();
            confirm.setWidescreen(wide != 0);
            openToList(confirm);
            confirm.update(press(Down));
            confirm.update(press(Down));
            confirm.update(press(Down));
            if (quit) {
                confirm.update(press(Down));
            }
            confirm.update(press(A));
            HM::View view = confirm.view();
            view.dimOpacity = 0.0f;  // leave only the panel and its contents
            view.footer[0] = '\0';
            HM::publish(view);
            const float w = wide ? 1280.0f : 960.0f;
            const float x = (1280.0f - w) * 0.5f;
            const Bounds panel = drawOverlay(x, 40.0f, w, 720.0f);
            const float px0 = x + (view.panel.x0 + 1.0f) * 0.5f * w;
            const float px1 = x + (view.panel.x1 + 1.0f) * 0.5f * w;
            // Allow for the border stroke (half its width) and the antialiasing fringe.
            const float stroke = 720.0f * 0.003f * 0.5f + 1.0f;
            check(panel.vertices > 0 && panel.x0 >= px0 - stroke && panel.x1 <= px1 + stroke,
                  "confirmation text fits the panel");
        }
    }

    // The Controls page at the game's 640x480 and in a 16:9 image: every row's
    // inputs text is drawn inside the panel and at least 8 pixels tall.
    for (int wide = 0; wide < 2; wide++) {
        HM::Menu page = newMenu();
        page.setWidescreen(wide != 0);
        const std::vector<HM::ControlsEntry> entries = sampleControls();
        page.setControls(entries.data(), static_cast<int>(entries.size()));
        openToList(page);
        page.update(press(Down));
        page.update(press(A));
        HM::View view = page.view();
        view.dimOpacity = 0.0f;
        HM::publish(view);
        const float w = wide ? 854.0f : 640.0f;
        const float h = 480.0f;
        ImGuiIO& pageIo = ImGui::GetIO();
        pageIo.DisplaySize = ImVec2(1280.0f, 800.0f);
        pageIo.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        HM::drawImGuiOverlay(0.0f, 0.0f, w, h);
        ImGui::Render();
        const float px0 = (view.panel.x0 + 1.0f) * 0.5f * w;
        const float px1 = (view.panel.x1 + 1.0f) * 0.5f * w;
        const float top = (view.linesArea.y0 + 1.0f) * 0.5f * h;
        const float rowHeight = (view.linesArea.y1 - view.linesArea.y0) * 0.5f * h / static_cast<float>(view.lineCount);
        std::vector<float> rowMin(view.lineCount, 1e9f);
        std::vector<float> rowMax(view.lineCount, -1e9f);
        bool inside = true;
        const ImDrawData* data = ImGui::GetDrawData();
        for (int i = 0; i < data->CmdListsCount; i++) {
            for (const ImDrawVert& v : data->CmdLists[i]->VtxBuffer) {
                const ImVec4 c = ImGui::ColorConvertU32ToFloat4(v.col);
                const bool keys = c.x > 0.99f && std::fabs(c.y - 0.92f) < 0.01f && std::fabs(c.z - 0.55f) < 0.01f;
                const bool pads = std::fabs(c.x - 0.62f) < 0.01f && std::fabs(c.y - 0.86f) < 0.01f && c.z > 0.99f;
                if (!keys && !pads) {
                    continue;  // not the inputs text (keyboard and controller columns)
                }
                inside = inside && v.pos.x >= px0 && v.pos.x <= px1;
                const int row = static_cast<int>((v.pos.y - top) / rowHeight);
                if (row >= 0 && row < view.lineCount) {
                    rowMin[row] = std::fmin(rowMin[row], v.pos.y);
                    rowMax[row] = std::fmax(rowMax[row], v.pos.y);
                }
            }
        }
        float smallest = 1e9f;
        for (int row = 1; row < view.lineCount; row++) {  // row 0: the header, in another colour
            smallest = std::fmin(smallest, rowMax[row] - rowMin[row]);
        }
        check(inside, "controls text stays inside the panel");
        check(smallest >= 8.0f, "controls text readable at 480 lines (" + std::to_string(smallest) + " px)");
    }

    // The title hint: a bar above the logo (top 8% of the image), text inside
    // it and readable at 480 lines; nothing without text.
    {
        HM::publish(HM::View{});
        const char* hint = "Keyboard: Return starts   |   F1: all controls";
        for (int wide = 0; wide < 2; wide++) {
            const float w = wide ? 854.0f : 640.0f;
            const float h = 480.0f;
            ImGuiIO& hintIo = ImGui::GetIO();
            hintIo.DisplaySize = ImVec2(1280.0f, 800.0f);
            hintIo.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            HM::drawTitleHint(hint, 0.0f, 0.0f, w, h);
            ImGui::Render();
            const HM::Rect r = HM::titleHintRect();
            const float bx0 = (r.x0 + 1.0f) * 0.5f * w, bx1 = (r.x1 + 1.0f) * 0.5f * w;
            const float by0 = (r.y0 + 1.0f) * 0.5f * h, by1 = (r.y1 + 1.0f) * 0.5f * h;
            float textTop = 1e9f, textBottom = -1e9f;
            bool inside = true;
            int vertices = 0;
            const ImDrawData* data = ImGui::GetDrawData();
            for (int i = 0; i < data->CmdListsCount; i++) {
                for (const ImDrawVert& v : data->CmdLists[i]->VtxBuffer) {
                    ++vertices;
                    inside = inside && v.pos.x >= bx0 - 1.0f && v.pos.x <= bx1 + 1.0f && v.pos.y >= by0 - 1.0f &&
                             v.pos.y <= by1 + 1.0f;
                    const ImVec4 c = ImGui::ColorConvertU32ToFloat4(v.col);
                    if (c.x > 0.99f && c.y > 0.99f && c.z > 0.99f) {  // the white text
                        textTop = std::fmin(textTop, v.pos.y);
                        textBottom = std::fmax(textBottom, v.pos.y);
                    }
                }
            }
            check(vertices > 100 && inside, "title hint drawn inside its bar");
            check(by1 <= 0.08f * h, "title hint above the logo (top 8% of the image)");
            check(textBottom - textTop >= 8.0f,
                  "title hint readable at 480 lines (" + std::to_string(textBottom - textTop) + " px)");
        }
        ImGui::NewFrame();
        HM::drawTitleHint(nullptr, 0.0f, 0.0f, 640.0f, 480.0f);
        HM::drawTitleHint("", 0.0f, 0.0f, 640.0f, 480.0f);
        ImGui::Render();
        int none = 0;
        for (int i = 0; i < ImGui::GetDrawData()->CmdListsCount; i++) {
            none += ImGui::GetDrawData()->CmdLists[i]->VtxBuffer.Size;
        }
        check(none == 0, "no title hint without text");
    }

    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    menu.update(press(Up));
    menu.update(press(A));
    for (int i = 0; i < 30; i++) {
        menu.update(none());
    }
    check(menu.selection() == HM::Selection::Restart, "black-out finished");
    menu.open();
    for (int i = 0; i < 10; i++) {
        menu.update(none());
    }
    menu.startBlackOut();
    for (int i = 0; i < 29; i++) {
        menu.update(none());
    }
    const float black = menu.view().blackOpacity;
    check(black > 0.95f && black < 1.0f, "nearly black one frame before the result");
    HM::publish(menu.view());
    const Bounds blackOut = drawOverlay(0.0f, 0.0f, 1280.0f, 800.0f);
    check(std::fabs(blackOut.blackAlpha - black) < 0.01f, "black-out draws its opacity over the image");
    check(blackOut.x0 <= 0.0f && blackOut.y1 >= 800.0f, "black-out covers the image");

    ImGui::DestroyContext();
}
#endif

}  // namespace

void testModsPage() {
    HM::Menu menu = newMenu();
    HM::ModsEntry entries[2];
    std::strcpy(entries[0].label, "Collect visible Star Bits");
    std::strcpy(entries[1].label, "Fire a Star Bit at the nearest enemy");
    entries[1].on = true;
    menu.setMods(entries, 2);
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Mods && menu.focus() == 0 && played(HM::Sound::Select), "Mods opens its page");
    HM::View view = menu.view();
    check(std::strcmp(view.title, "Mods") == 0 && view.itemCount == 5 &&
              std::strcmp(view.items[0].label, "Collect visible Star Bits: Off") == 0 &&
              std::strcmp(view.items[1].label, "Fire a Star Bit at the nearest enemy: On") == 0 &&
              std::strcmp(view.items[2].label, "Odyssey camera...") == 0 &&
              std::strcmp(view.items[3].label, "Level Select") == 0 && std::strcmp(view.items[4].label, "Back") == 0,
          "Mods page shows each toggle's state, the camera options, Level Select and Back");
    check(menu.takeModToggles() == 0, "nothing toggled yet");
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Mods && menu.mods()[0].on &&
              std::strcmp(menu.view().items[0].label, "Collect visible Star Bits: On") == 0,
          "A toggles the focused mod on and relabels it");
    check(menu.takeModToggles() == 1u && menu.takeModToggles() == 0, "the toggle is reported once");
    menu.update(press(Down));
    menu.update(press(A));
    check(!menu.mods()[1].on && menu.takeModToggles() == 2u, "second toggle turns off");
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    check(menu.focus() == 4, "Back is last");
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::List && menu.focus() == 2 && played(HM::Sound::Cancel), "Back returns to Mods");
    menu.update(press(A));
    menu.update(press(Plus | B));
    check(menu.phase() == HM::Phase::List && menu.focus() == 2 && menu.takeModToggles() == 0, "Escape returns, no toggle");
    menu.update(press(A));
    check(std::strcmp(menu.view().items[0].label, "Collect visible Star Bits: On") == 0, "state kept on reopen");
}

// Regression: with every mod slot filled and the Mod folder page present, the Mods page
// needs kMaxMods + 4 items. It once held 8 for 5 mods (9 items), and items[8] overwrote
// View::lineCount, so the overlay's Controls loop read off the stack (SIGSEGV on F1 -> Mods).
void testModsPageCapacity() {
    HM::Menu menu = newMenu();
    HM::ModsEntry entries[HM::kMaxMods];
    for (int i = 0; i < HM::kMaxMods; ++i) {
        std::snprintf(entries[i].label, sizeof(entries[i].label), "Mod %d", i);
    }
    menu.setMods(entries, HM::kMaxMods);
    HM::FolderModEntry folder[2];
    std::strcpy(folder[0].label, "Folder mod");
    menu.setFolderMods(folder, 1);
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Mods, "full Mods page opens");
    const HM::View view = menu.view();
    check(view.itemCount == HM::kMaxMods + 4 && view.itemCount <= HM::kMaxItems, "Mods page item count fits View::items");
    check(view.lineCount == 0, "Mods page has no Controls lines (items must not overwrite lineCount)");
    bool labels = true;
    bool onScreen = true;
    for (int i = 0; i < view.itemCount; ++i) {
        labels = labels && view.items[i].label != nullptr && view.items[i].label[0] != '\0';
        onScreen = onScreen && view.items[i].rect.y0 >= -1.0f && view.items[i].rect.y1 <= 0.915f;
    }
    check(labels, "every Mods page item has a label");
    check(onScreen, "every Mods page item sits above the footer");
    check(std::strcmp(view.items[HM::kMaxMods].label, "Odyssey camera...") == 0 &&
              std::strcmp(view.items[HM::kMaxMods + 1].label, "Level Select") == 0 &&
              std::strcmp(view.items[HM::kMaxMods + 2].label, "Mod folder...") == 0 &&
              std::strcmp(view.items[HM::kMaxMods + 3].label, "Back") == 0,
          "the fixed rows follow the last mod toggle");
    // Every row can take the focus in turn, top to bottom, and exactly one is focused at a time.
    bool walked = true;
    for (int i = 0; i < view.itemCount; ++i) {
        const HM::View now = menu.view();
        int focused = 0;
        for (int j = 0; j < now.itemCount; ++j) focused += now.items[j].focused ? 1 : 0;
        walked = walked && menu.focus() == i && focused == 1 && now.items[i].focused;
        menu.update(press(Down));
    }
    check(walked, "focus walks every row of the full Mods page");
    for (int i = 0; i < view.itemCount; ++i) menu.update(press(Up));
    check(menu.focus() == 0, "and back up to the first");
    // The last toggle works, and Back is reachable and leaves the page.
    for (int i = 0; i < HM::kMaxMods - 1; ++i) menu.update(press(Down));
    menu.update(press(A));
    check(menu.takeModToggles() == (1u << (HM::kMaxMods - 1)), "the last mod toggle works");
    for (int i = 0; i < 20; ++i) menu.update(press(Down));
    check(menu.focus() == view.itemCount - 1, "focus reaches the last item");
    menu.update(press(A));
    check(menu.phase() == HM::Phase::List && menu.focus() == 2, "Back is reachable on a full Mods page");
    // Escape leaves it too.
    menu.update(press(A));
    menu.update(press(Plus | B));
    check(menu.phase() == HM::Phase::List, "Escape leaves a full Mods page");
}

void testModFolderPage() {
    HM::Menu menu = newMenu();
    HM::ModsEntry entries[1];
    std::strcpy(entries[0].label, "Collect visible Star Bits");
    menu.setMods(entries, 1);
    // Without setFolderMods the Mods page has no Mod folder entry.
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.view().itemCount == 4 && std::strcmp(menu.view().items[3].label, "Back") == 0, "no Mod folder entry before folder mods are set");
    menu.update(press(Plus));

    // Seven mods: three pages of three, three and one.
    HM::FolderModEntry folder[7];
    for (int i = 0; i < 7; ++i) {
        std::snprintf(folder[i].label, sizeof(folder[i].label), "Mod%d (%d files)", i, i + 1);
    }
    folder[4].on = true;
    menu.setFolderMods(folder, 7);
    menu.update(press(A));  // reopen the Mods page
    HM::View mods = menu.view();
    check(mods.itemCount == 5 && std::strcmp(mods.items[2].label, "Level Select") == 0 &&
              std::strcmp(mods.items[3].label, "Mod folder...") == 0 && std::strcmp(mods.items[4].label, "Back") == 0,
          "Mods page gains Mod folder before Back once folder mods are set");
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    check(menu.focus() == 3, "focus reaches Mod folder");
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::ModFolder && menu.focus() == 0 && played(HM::Sound::Select), "Mod folder opens");
    HM::View view = menu.view();
    check(std::strcmp(view.title, "Mod folder") == 0 && view.itemCount == 5 &&
              std::strcmp(view.items[0].label, "Mod0 (1 files): Off") == 0 && std::strcmp(view.items[2].label, "Mod2 (3 files): Off") == 0 &&
              std::strcmp(view.items[3].label, "Next page") == 0 && std::strcmp(view.items[4].label, "Back") == 0 &&
              std::strstr(view.message, "Page 1 of 3") != nullptr,
          "first page: three toggles, Next page, Back");
    check(menu.takeFolderToggles() == 0, "nothing toggled yet");
    menu.update(press(A));
    check(menu.folderMods()[0].on && std::strcmp(menu.view().items[0].label, "Mod0 (1 files): On") == 0 && menu.takeFolderToggles() == 1u &&
              menu.takeFolderToggles() == 0,
          "A toggles the mod and reports it once");
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.focus() == 0 && std::strstr(menu.view().message, "Page 2 of 3") != nullptr && std::strcmp(menu.view().items[1].label, "Mod4 (5 files): On") == 0,
          "Next page shows the next three and keeps their state");
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.folderMods()[4].on == false && menu.takeFolderToggles() == (std::uint64_t{1} << 4), "toggle on page two maps to its entry");
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    check(menu.view().itemCount == 3 && std::strcmp(menu.view().items[0].label, "Mod6 (7 files): Off") == 0, "last page: one toggle, Next page, Back");
    menu.update(press(Down));
    menu.update(press(Down));
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Mods && menu.focus() == 3 && played(HM::Sound::Cancel), "Back returns to the Mods page on the Mod folder entry");
    menu.update(press(A));
    menu.update(press(Plus | B));
    check(menu.phase() == HM::Phase::Mods && menu.focus() == 3, "Escape returns from Mod folder");

    // No mods found: the page says so and offers Back.
    HM::Menu empty = newMenu();
    empty.setMods(entries, 1);
    empty.setFolderMods(nullptr, 0);
    openToList(empty);
    empty.update(press(Down));
    empty.update(press(Down));
    empty.update(press(A));
    for (int i = 0; i < 3; ++i) empty.update(press(Down));
    empty.update(press(A));
    check(empty.phase() == HM::Phase::ModFolder && empty.view().itemCount == 1 && std::strcmp(empty.view().items[0].label, "Back") == 0 &&
              std::strstr(empty.view().message, "No mods found") != nullptr,
          "no folder mods: message and Back");
}

void testProgressPage() {
    namespace PR = PetariNative::Progress;
    PR::resetForTesting();
    std::string error;
    check(PR::parse("{\"version\": 1, \"missions\": {"
                    "\"EggStarGalaxy:1\": {\"stage\": \"EggStarGalaxy\", \"mission\": 1, \"first_clear\": \"2026-10-01T10:00:00Z\", "
                    "\"last_clear\": \"2026-10-03T12:00:00Z\", \"clears\": 3, \"best_time_s\": 123.5, \"fewest_deaths\": 0, "
                    "\"best_coins\": 42, \"best_star_bits\": 120},"
                    "\"EggStarGalaxy:5\": {\"stage\": \"EggStarGalaxy\", \"mission\": 5, \"first_clear\": \"2026-10-02T09:00:00Z\", "
                    "\"clears\": 1, \"best_time_s\": 59.0, \"fewest_deaths\": 2, \"best_coins\": 0, \"best_star_bits\": 0}}}", &error),
          "test record parses");
    HM::Menu menu = newMenu();
    openToList(menu);
    for (int i = 0; i < 3; ++i) menu.update(press(Down));
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Progress && menu.focus() == 0 && played(HM::Sound::Select), "My Progress opens from the list");
    HM::View view = menu.view();
    check(std::strcmp(view.title, "My Progress") == 0 && view.itemCount == 4 && std::strcmp(view.items[1].label, "Reset progress...") == 0 &&
              std::strcmp(view.items[2].label, "Badges: On") == 0 && std::strcmp(view.items[3].label, "Back") == 0,
          "title, selector, Reset, Badges and Back");
    check(std::strstr(view.items[0].label, "Bowser Jr.'s Robot Reactor") != nullptr && view.lineCount == 2 &&
              std::strstr(view.lines[1].inputs, "Not cleared yet") != nullptr,
          "starts on the first galaxy, a one-mission galaxy has one row under the header");
    menu.update(press(Right));
    menu.update(press(Right));
    view = menu.view();
    check(std::strstr(view.items[0].label, "Good Egg Galaxy") != nullptr && view.lineCount == 7, "Right moves to the next galaxies; Good Egg has six missions");
    check(std::strstr(view.message, "2 of 6 cleared here, 2 in all") != nullptr && std::strstr(view.message, "save is not changed") != nullptr,
          "the message counts clears here and in all");
    check(std::strcmp(view.lines[1].action, "Mission 1") == 0 && std::strstr(view.lines[1].inputs, "Cleared 2:03.50  0  42  120") != nullptr &&
              std::strstr(view.lines[1].pad, "x3  2026-10-01") != nullptr,
          "a cleared mission shows best time, deaths, coins, Star Bits, clears and first clear date");
    check(std::strcmp(view.lines[4].action, "Mission 4 (comet)") == 0 && std::strstr(view.lines[4].inputs, "Not cleared yet") != nullptr &&
              std::strcmp(view.lines[6].action, "Mission 6 (hidden star)") == 0 &&
              std::strstr(view.lines[5].inputs, "Cleared 0:59.00  2  0  0") != nullptr,
          "comet and hidden missions are labelled; mission 5 is cleared");
    menu.update(press(Left));
    check(std::strstr(menu.view().items[0].label, "Honeyhive") == nullptr && std::strstr(menu.view().items[0].label, "Flipswitch Galaxy") != nullptr,
          "Left moves back");
    for (int i = 0; i < 40; ++i) menu.update(press(Left));
    check(std::strstr(menu.view().items[0].label, "<  ") != nullptr, "the selector wraps around");

    // Reset needs two presses and is disarmed by moving away.
    menu.update(press(Down));
    sounds.clear();
    menu.update(press(A));
    check(std::strcmp(menu.view().items[1].label, "Press A again to erase everything") == 0 && PR::clearedCount() == 2, "first press arms the reset");
    menu.update(press(Up));
    menu.update(press(Down));
    check(std::strcmp(menu.view().items[1].label, "Reset progress...") == 0 && PR::clearedCount() == 2, "moving away disarms it");
    menu.update(press(A));
    menu.update(press(A));
    check(PR::clearedCount() == 0 && std::strcmp(menu.view().items[1].label, "Reset progress...") == 0, "second press erases the record");

    // The badge setting toggles and persists in the settings file; the record is untouched.
    menu.update(press(Down));
    menu.update(press(A));
    check(!PR::badgesEnabled() && std::strcmp(menu.view().items[2].label, "Badges: Off") == 0, "A on Badges turns the badges off");
    menu.update(press(A));
    check(PR::badgesEnabled() && std::strcmp(menu.view().items[2].label, "Badges: On") == 0, "and on again");

    // Back and Escape return to the list on My Progress.
    menu.update(press(Down));
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::List && menu.focus() == 3 && played(HM::Sound::Cancel), "Back returns to the list");
    menu.update(press(A));
    menu.update(press(Plus | B));
    check(menu.phase() == HM::Phase::List && menu.focus() == 3, "Escape returns too");
    PR::resetForTesting();
}

void testLevelSelect() {
    namespace LS = PetariNative::App::LaunchStage;
    HM::Menu menu = newMenu();
    HM::ModsEntry entries[2];
    std::strcpy(entries[0].label, "Collect visible Star Bits");
    std::strcpy(entries[1].label, "Fire a Star Bit at the nearest enemy");
    menu.setMods(entries, 2);
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Levels && menu.focus() == 0, "Level Select opens from the Mods page");
    int count = 0;
    const LS::Galaxy* galaxies = LS::galaxies(&count);
    HM::View view = menu.view();
    check(std::strcmp(view.title, "Level Select") == 0 && view.itemCount == 4 &&
              std::strcmp(view.items[0].label, galaxies[0].name) == 0 &&
              std::strcmp(view.items[1].label, "Mission 1 of 1") == 0 && std::strcmp(view.items[2].label, "Go") == 0 &&
              std::strcmp(view.items[3].label, "Back") == 0 && std::strstr(view.message, "Terrace") != nullptr,
          "Level Select shows galaxy, mission, Go and Back");
    menu.update(press(HM::Button::Left));
    check(menu.levelGalaxy() == count - 1, "editor off wraps to the last real galaxy");
    menu.update(press(HM::Button::Right));
    // Right steps through galaxies; Left wraps backward.
    int goodEgg = -1;
    for (int i = 0; i < count; i++) {
        if (std::strcmp(galaxies[i].stage, "EggStarGalaxy") == 0) goodEgg = i;
    }
    for (int i = 0; i < goodEgg; i++) {
        menu.update(press(HM::Button::Right));
    }
    check(menu.levelGalaxy() == goodEgg && std::strcmp(menu.view().items[0].label, "Good Egg Galaxy") == 0, "Right steps galaxies");
    menu.update(press(HM::Button::Left));
    menu.update(press(HM::Button::Right));
    check(menu.levelGalaxy() == goodEgg, "Left steps back");
    menu.update(press(Down));
    for (int i = 0; i < 3; i++) {
        menu.update(press(HM::Button::Right));
    }
    check(menu.levelMission() == 4 && std::strcmp(menu.view().items[1].label, "Mission 4 of 6 (comet)") == 0,
          "mission steps, comet marked");
    menu.update(press(A));
    menu.update(press(A));
    check(std::strcmp(menu.view().items[1].label, "Mission 6 of 6 (hidden star)") == 0, "A also steps; hidden star marked");
    menu.update(press(HM::Button::Right));
    check(menu.levelMission() == 1, "missions wrap");
    menu.update(press(HM::Button::Left));
    check(menu.levelMission() == 6, "missions wrap backward");
    const char* stage = nullptr;
    int mission = 0;
    check(!menu.takeLevelRequest(&stage, &mission), "no request before Go");
    menu.update(press(Down));
    sounds.clear();
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Closing && played(HM::Sound::Select), "Go closes the menu");
    check(!menu.takeLevelRequest(&stage, &mission), "request waits for the menu to finish closing");
    check(framesUntilSelection(menu) == 10 && menu.selection() == HM::Selection::Resume, "Go resumes the game");
    check(menu.takeLevelRequest(&stage, &mission) && std::strcmp(stage, "EggStarGalaxy") == 0 && mission == 6,
          "the warp is reported once the menu is closed");
    check(!menu.takeLevelRequest(&stage, &mission), "reported once");
    // Back and Escape return to the Mods page without a request.
    menu.open();
    for (int i = 0; i < 10; i++) menu.update(none());
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    menu.update(press(Plus | B));
    check(menu.phase() == HM::Phase::Mods && menu.focus() == 3, "Escape returns to the Mods page");
    check(!menu.takeLevelRequest(&stage, &mission), "no request after Back");
    menu.setEditorEnabled(true);
    menu.update(press(A));
    while (menu.levelGalaxy() != count) menu.update(press(HM::Button::Right));
    check(std::strstr(menu.view().items[0].label, "New galaxy...") != nullptr &&
              std::strcmp(menu.view().items[2].label, "Create...") == 0 &&
              std::strstr(menu.view().message, "Mod galaxies") != nullptr,
          "editor on adds New galaxy after the mod destinations");
    menu.update(press(HM::Button::Right));
    check(menu.levelGalaxy() == 0, "New galaxy wraps to the first real galaxy");
    menu.update(press(HM::Button::Left));
    check(menu.levelGalaxy() == count, "Left from the first galaxy reaches New galaxy");
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.levelMission() == 1, "New galaxy has no mission selector to index");
    menu.update(press(Down));
    menu.update(press(A));
    check(!menu.takeNewGalaxyRequest(), "New galaxy waits for Home to close");
    framesUntilSelection(menu);
    check(!menu.takeLevelRequest(&stage, &mission), "New galaxy never creates a warp");
    check(menu.takeNewGalaxyRequest() && !menu.takeNewGalaxyRequest(), "New galaxy reports one editor request");
    menu.setEditorEnabled(false);
    check(menu.levelGalaxy() == 0, "turning editor off drops the sentinel selection");

}

void testCameraPage() {
    HM::Menu menu = newMenu();
    HM::ModsEntry entries[3];
    std::strcpy(entries[0].label, "Collect visible Star Bits");
    std::strcpy(entries[1].label, "Fire a Star Bit at the nearest enemy");
    std::strcpy(entries[2].label, "Odyssey movement");
    menu.setMods(entries, 3);
    HM::CameraOptions options;
    options.speed = 4;
    menu.setCamera(options);
    openToList(menu);
    menu.update(press(Down));
    menu.update(press(Down));
    menu.update(press(A));
    HM::View mods = menu.view();
    check(mods.itemCount == 6 && std::strcmp(mods.items[3].label, "Odyssey camera...") == 0, "six-item Mods page");
    check(mods.panel.y0 >= -1.0f && mods.panel.y1 <= 1.0f && mods.items[5].rect.y1 < mods.panel.y1 &&
              mods.items[0].rect.y1 <= mods.items[1].rect.y0,
          "six items fit without overlapping");
    for (int i = 0; i < 3; i++) menu.update(press(Down));
    menu.update(press(A));
    check(menu.phase() == HM::Phase::Camera && menu.focus() == 0, "camera options open");
    HM::View view = menu.view();
    check(std::strcmp(view.title, "Odyssey camera") == 0 && view.itemCount == 6 &&
              std::strcmp(view.items[0].label, "Odyssey camera: Off") == 0 && std::strcmp(view.items[1].label, "Speed: 4") == 0 &&
              std::strcmp(view.items[2].label, "Invert horizontal: Off") == 0 &&
              std::strcmp(view.items[3].label, "Invert vertical: Off") == 0 &&
              std::strcmp(view.items[4].label, "Photo mode (P): Off") == 0 && std::strcmp(view.items[5].label, "Back") == 0,
          "camera page shows its options");
    check(view.items[5].rect.y1 < view.panel.y1 && view.items[4].rect.y1 <= view.items[5].rect.y0, "six camera items fit");
    HM::CameraOptions changed;
    check(!menu.takeCameraChange(&changed), "no change yet");
    menu.update(press(A));
    check(menu.takeCameraChange(&changed) && changed.on && !menu.takeCameraChange(&changed), "A turns the camera on, reported once");
    menu.update(press(Down));
    menu.update(press(HM::Button::Right));
    menu.update(press(HM::Button::Right));
    check(menu.camera().speed == 1 && std::strcmp(menu.view().items[1].label, "Speed: 1") == 0, "speed wraps 5 -> 1");
    menu.update(press(HM::Button::Left));
    check(menu.camera().speed == 5, "speed wraps back");
    menu.update(press(Down));
    menu.update(press(A));
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.takeCameraChange(&changed) && changed.invertX && changed.invertY && changed.speed == 5, "inverts toggle");
    menu.update(press(Down));
    menu.update(press(A));
    check(menu.takeCameraChange(&changed) && changed.photo && changed.on &&
              std::strcmp(menu.view().items[4].label, "Photo mode (P): On") == 0,
          "photo mode toggles on its own");
    menu.update(press(Plus | B));
    check(menu.phase() == HM::Phase::Mods && menu.focus() == 3, "Escape returns to the camera entry");
}

int main() {
    testLifecycle();
    testSelections();
    testCancellation();
    testBlackOut();
    testFocusAndRepeat();
    testControlsPage();
    testRemapper();
    testRemapperScrolling();
    testModsPage();
    testModsPageCapacity();
    testModFolderPage();
    testProgressPage();
    testLevelSelect();
    testCameraPage();
    testBridgeInput();
    testWrapper();
#ifdef PETARI_HOME_MENU_TEST_IMGUI
    testImGuiOverlay();
#endif
    std::printf("native home menu tests passed (%d checks)\n", checks);
    return 0;
}
