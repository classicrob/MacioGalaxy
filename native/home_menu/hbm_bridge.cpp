// The HBM entry points of HomeButtonMenuWrapper.cpp, served by the native menu.
// Compiled with the SDK headers; home_menu.cpp stays free of them.

#include "petari/home_menu_hbm.hpp"
#include "petari/player_launch.hpp"

#include <cstdio>

#include <revolution/wpad.h>

#ifdef PETARI_HOME_MENU_INPUT
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "petari/host_allocation.hpp"
#include "petari/input.hpp"
#include "petari/input_remap.hpp"
#include "petari/mods.hpp"
#include "petari/platform/mod_folder.hpp"
#include "petari/camera_settings.hpp"
#endif

namespace PetariNative::HomeMenu {

static_assert(static_cast<int>(Selection::None) == HBM_SELECT_NULL);
static_assert(static_cast<int>(Selection::Resume) == HBM_SELECT_HOMEBTN);
static_assert(static_cast<int>(Selection::Quit) == HBM_SELECT_BTN1);
static_assert(static_cast<int>(Selection::Restart) == HBM_SELECT_BTN2);
static_assert(Sound::EventBeginExitAnim == HBMSEV_BEGIN_EXIT_ANIM);
static_assert(Sound::EventBeginBlackOut == HBMSEV_BEGIN_BLACKOUT);
static_assert(Sound::EventEndMenu == HBMSEV_END_MENU);
static_assert(Sound::EventPlaySound == HBMSEV_PLAY_SOUND);
static_assert(Sound::HomeButton == HBMSE_HOME_BUTTON);
static_assert(Sound::ReturnApp == HBMSE_RETURN_APP);
static_assert(Sound::GotoMenu == HBMSE_GOTO_MENU);
static_assert(Sound::ResetApp == HBMSE_RESET_APP);
static_assert(Sound::Focus == HBMSE_FOCUS);
static_assert(Sound::Select == HBMSE_SELECT);
static_assert(Sound::Cancel == HBMSE_CANCEL);

namespace {

// A Nunchuk stick past this reads as Up or Down.
constexpr float kStickThreshold = 0.5f;

std::uint32_t gStickHold[WPAD_MAX_CONTROLLERS];

std::uint32_t buttons(u32 wpad) {
    std::uint32_t bits = 0;
    if (wpad & WPAD_BUTTON_UP) {
        bits |= Button::Up;
    }
    if (wpad & WPAD_BUTTON_DOWN) {
        bits |= Button::Down;
    }
    if (wpad & WPAD_BUTTON_LEFT) {
        bits |= Button::Left;
    }
    if (wpad & WPAD_BUTTON_RIGHT) {
        bits |= Button::Right;
    }
    if (wpad & WPAD_BUTTON_A) {
        bits |= Button::A;
    }
    if (wpad & WPAD_BUTTON_B) {
        bits |= Button::B;
    }
    if (wpad & WPAD_BUTTON_PLUS) {
        bits |= Button::Plus;
    }
    if (wpad & WPAD_BUTTON_HOME) {
        bits |= Button::Home;
    }
    if (wpad & WPAD_BUTTON_C) {
        bits |= Button::C;
    }
    if (wpad & WPAD_BUTTON_Z) {
        bits |= Button::Z;
    }
    return bits;
}

}  // namespace

FrameInput frameInput(const HBMControllerData* controllers) {
    FrameInput input;
    if (controllers == nullptr) {
        return input;
    }
    for (int chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        const HBMKPadData& pad = controllers->wiiCon[chan];
        const KPADStatus* status = pad.kpad;
        if (status == nullptr) {
            gStickHold[chan] = 0;
            continue;
        }
        input.hold |= buttons(status->hold);
        input.trigger |= buttons(status->trig);

        std::uint32_t stick = 0;
        if (pad.use_devtype == WPAD_DEV_FREESTYLE) {
            const float x = status->ex_status.fs.stick.x;
            const float y = status->ex_status.fs.stick.y;
            if (y > kStickThreshold) {
                stick = Button::Up;
            } else if (y < -kStickThreshold) {
                stick = Button::Down;
            } else if (x < -kStickThreshold) {
                stick = Button::Left;
            } else if (x > kStickThreshold) {
                stick = Button::Right;
            }
        }
        input.hold |= stick;
        input.trigger |= stick & ~gStickHold[chan];
        gStickHold[chan] = stick;

        if (!input.pointerValid && status->dpd_valid_fg > 0) {
            input.pointerValid = true;
            input.pointerX = pad.pos.x;
            input.pointerY = pad.pos.y;
        }
    }
    return input;
}

namespace Hbm {

void create(const HBMDataInfo* info) {
    Config config;
    if (info != nullptr) {
        config.sound = info->sound_callback;
        config.frameDelta = info->frameDelta;
        config.widescreenAdjustX = info->adjust.x;
    }
    instance().create(config);
    publish(View{});
}

#ifdef PETARI_HOME_MENU_INPUT
void refreshFooterKeys();

// The Controls page, from the bindings in effect now (controls.txt included).
void refreshControls() {
    HostAllocationScope scope;  // HBMInit runs on a game thread
    const std::vector<Input::ControlsLine> summary = Input::controlsSummary(Input::bindings());
    ControlsEntry entries[kMaxControls];
    const int count = std::min(static_cast<int>(summary.size()), kMaxControls);
    for (int i = 0; i < count; i++) {
        std::strncpy(entries[i].action, summary[i].action.c_str(), sizeof(entries[i].action) - 1);
        std::strncpy(entries[i].inputs, summary[i].inputs.c_str(), sizeof(entries[i].inputs) - 1);
        std::strncpy(entries[i].pad, summary[i].pad.c_str(), sizeof(entries[i].pad) - 1);
    }
    instance().setControls(entries, count);
    refreshFooterKeys();
}

// The footer's key names: each button's first keyboard or mouse input (arrow keys for
// the + Control Pad's left and right when they are bound).
void refreshFooterKeys() {
    HostAllocationScope scope;
    const Input::Bindings bindings = Input::bindings();
    auto key = [&bindings](Input::Action action, Input::KeyCode prefer) {
        const auto& inputs = bindings.inputs(action);
        for (const Input::Binding& input : inputs) {
            if (input.device == Input::Binding::Device::Key && input.code == prefer) return Input::displayName(input);
        }
        for (const Input::Binding& input : inputs) {
            if (input.device != Input::Binding::Device::Pad) return Input::displayName(input);
        }
        return std::string();
    };
    const std::string left = key(Input::Action::DpadLeft, Input::Key::Left);
    const std::string right = key(Input::Action::DpadRight, Input::Key::Right);
    const std::string change = left == "Left arrow" && right == "Right arrow" ? std::string("\xE2\x86\x90/\xE2\x86\x92")  // U+2190/U+2192
                               : left.empty() || right.empty() ? left + right : left + "/" + right;
    instance().setFooterKeys(key(Input::Action::A, 0).c_str(), key(Input::Action::Plus, 0).c_str(),
                             key(Input::Action::Home, 0).c_str(), change.c_str());
}

// The Controls page's remapper on the live bindings, saved to controls.txt at
// once (petari/input_remap.hpp).
class InputControls final : public ControlsBackend {
public:
    int rowCount() override { return static_cast<int>(sizeof(kRows) / sizeof(kRows[0])); }

    void row(int index, RemapRow* out) override {
        HostAllocationScope scope;
        const Input::Bindings bindings = Input::bindings();
        static const Input::Bindings kDefaults = Input::Bindings::defaults();
        const Row& row = kRows[index];
        std::snprintf(out->label, sizeof(out->label), "%s%s%s", row.icon, row.icon[0] != '\0' ? " " : "", row.label);
        for (int slot = 0; slot < kRemapSlots; slot++) {
            Input::Binding input;
            out->slots[slot][0] = '\0';
            out->conflict[slot] = false;
            if (Input::Remap::slotInput(bindings, row.action, slot, &input)) {
                std::snprintf(out->slots[slot], sizeof(out->slots[slot]), "%s", name(input).c_str());
                out->conflict[slot] = !Input::Remap::conflicts(bindings, row.action, input).empty();
            }
        }
        out->changed = bindings.inputs(row.action) != kDefaults.inputs(row.action);
    }

    void beginCapture() override { Input::beginCapture(); }
    void cancelCapture() override { Input::cancelCapture(); }

    CaptureStatus pollCapture(char* out, int size) override {
        Input::Binding input;
        switch (Input::captureState(&input)) {
        case Input::CaptureState::Captured: {
            HostAllocationScope scope;
            mCaptured = input;
            std::snprintf(out, static_cast<size_t>(size), "%s", name(input).c_str());
            return CaptureStatus::Captured;
        }
        case Input::CaptureState::Cancelled:
            return CaptureStatus::Cancelled;
        case Input::CaptureState::Waiting:
            return CaptureStatus::Waiting;
        case Input::CaptureState::Idle:
            break;
        }
        return CaptureStatus::Cancelled;  // the capture went away
    }

    RemapOutcome place(int row, int slot, RemapChoice choice, char* other, int size) override {
        HostAllocationScope scope;
        Input::Bindings bindings = Input::bindings();
        const Input::Action action = kRows[row].action;
        const Input::Remap::Resolve resolve = choice == RemapChoice::Swap       ? Input::Remap::Resolve::Swap
                                              : choice == RemapChoice::KeepBoth ? Input::Remap::Resolve::KeepBoth
                                                                                : Input::Remap::Resolve::Check;
        const Input::Remap::Result result = Input::Remap::assign(bindings, action, slot, mCaptured, resolve);
        if (result == Input::Remap::Result::Conflict) {
            const auto others = Input::Remap::boundElsewhere(bindings, action, mCaptured);
            std::snprintf(other, static_cast<size_t>(size), "%s", others.empty() ? "in use" : labelOf(others.front()));
        }
        return apply(bindings, result);
    }

    RemapOutcome clearSlot(int row, int slot) override {
        HostAllocationScope scope;
        Input::Bindings bindings = Input::bindings();
        return apply(bindings, Input::Remap::clearSlot(bindings, kRows[row].action, slot));
    }

    RemapOutcome resetRow(int row) override {
        HostAllocationScope scope;
        Input::Bindings bindings = Input::bindings();
        return apply(bindings, Input::Remap::resetAction(bindings, kRows[row].action));
    }

    RemapOutcome resetAll() override {
        HostAllocationScope scope;
        Input::Bindings bindings = Input::bindings();
        const Input::Bindings defaults = Input::Bindings::defaults();
        if (bindings.serialize() == defaults.serialize()) return RemapOutcome::Unchanged;
        return apply(defaults, Input::Remap::Result::Done);
    }

    void notice(char* out, int size) override {
        HostAllocationScope scope;
        const std::string message = mSaveError.empty() ? Input::Remap::loadMessage() : mSaveError;
        std::snprintf(out, static_cast<size_t>(size), "%s", message.c_str());
    }

private:
    struct Row {
        Input::Action action;
        const char* icon;
        const char* label;
    };
    // Every action, in the order a player looks for them.
    static constexpr Row kRows[] = {
        {Input::Action::StickUp, PETARI_ICON_STICK, "Move forward"},
        {Input::Action::StickDown, PETARI_ICON_STICK, "Move back"},
        {Input::Action::StickLeft, PETARI_ICON_STICK, "Move left"},
        {Input::Action::StickRight, PETARI_ICON_STICK, "Move right"},
        {Input::Action::A, PETARI_ICON_A, "Jump / confirm"},
        {Input::Action::Shake, PETARI_ICON_SHAKE, "Spin"},
        {Input::Action::NunchukZ, PETARI_ICON_Z, "Crouch / ground pound"},
        {Input::Action::B, PETARI_ICON_B, "Shoot Star Bits / back"},
        {Input::Action::Start, PETARI_ICON_A, "Start (title: A and B)"},
        {Input::Action::NunchukC, PETARI_ICON_C, "Recenter camera"},
        {Input::Action::DpadLeft, PETARI_ICON_PAD, "Rotate camera left"},
        {Input::Action::DpadRight, PETARI_ICON_PAD, "Rotate camera right"},
        {Input::Action::DpadUp, PETARI_ICON_PAD, "First-person view"},
        {Input::Action::DpadDown, PETARI_ICON_PAD, "Leave first-person view"},
        {Input::Action::Plus, PETARI_ICON_PLUS, "Pause"},
        {Input::Action::Minus, PETARI_ICON_MINUS, "Minus (also pauses)"},
        {Input::Action::Home, PETARI_ICON_HOME, "This menu"},
        {Input::Action::Walk, "", "Walk slowly (hold)"},
        {Input::Action::TiltHold, PETARI_ICON_SHAKE, "Tilt the remote (hold)"},
        {Input::Action::PostureToggle, PETARI_ICON_SHAKE, "Upright remote (toggle)"},
        {Input::Action::One, PETARI_ICON_ONE, "1 button"},
        {Input::Action::Two, PETARI_ICON_TWO, "2 button"},
        {Input::Action::ModCappyThrow, "", "Cap throw (Cappy mod)"},
        {Input::Action::ModCollectStarBits, "", "Collect Star Bits (mod)"},
        {Input::Action::ModShootEnemy, "", "Shoot nearest enemy (mod)"},
        {Input::Action::CameraOrbitHold, "", "Camera mod: orbit (hold)"},
        {Input::Action::CameraOrbitLeft, "", "Camera mod: orbit left"},
        {Input::Action::CameraOrbitRight, "", "Camera mod: orbit right"},
        {Input::Action::CameraPitchUp, "", "Camera mod: pitch up"},
        {Input::Action::CameraPitchDown, "", "Camera mod: pitch down"},
        {Input::Action::CameraZoomIn, "", "Camera mod: zoom in"},
        {Input::Action::CameraZoomOut, "", "Camera mod: zoom out"},
        {Input::Action::PhotoMode, "", "Photo mode"},
        {Input::Action::PhotoShot, "", "Photo mode: save a shot"},
        {Input::Action::LevelEditor, "", "Level editor: Edit / Play (tap)"},
    };
    static_assert(sizeof(kRows) / sizeof(kRows[0]) == static_cast<size_t>(Input::Action::Count), "a row per action");

    // Controller buttons without the "Pad " prefix: the column says which device.
    static std::string name(const Input::Binding& input) {
        std::string text = Input::displayName(input);
        if (input.device == Input::Binding::Device::Pad && text.rfind("Pad ", 0) == 0) text = text.substr(4);
        return text;
    }

    static const char* labelOf(Input::Action action) {
        for (const Row& row : kRows) {
            if (row.action == action) return row.label;
        }
        return Input::actionName(action);
    }

    RemapOutcome apply(const Input::Bindings& bindings, Input::Remap::Result result) {
        switch (result) {
        case Input::Remap::Result::Conflict: return RemapOutcome::Conflict;
        case Input::Remap::Result::Unchanged: return RemapOutcome::Unchanged;
        case Input::Remap::Result::HomeUnreachable: return RemapOutcome::HomeUnreachable;
        case Input::Remap::Result::Done: break;
        }
        Input::setBindings(bindings);  // live at once
        std::string error;
        mSaveError.clear();
        if (!Input::Remap::save(bindings, &error)) {
            mSaveError = "Could not save controls.txt (" + error + "); the change lasts until you quit.";
            std::fprintf(stderr, "petari: controls: %s\n", error.c_str());
        }
        refreshFooterKeys();
        return RemapOutcome::Done;
    }

    Input::Binding mCaptured;
    std::string mSaveError;
};

InputControls gInputControls;

// The Mods page has one toggle per mod; raise kMaxMods (home_menu.hpp) when adding past it.
static_assert(static_cast<int>(Mods::Mod::Count) <= kMaxMods, "more Mods::Mod entries than the Mods page can list");

// The Mods page toggles, from the mod settings in effect now.
void refreshMods() {
    ModsEntry entries[kMaxMods];
    const int count = std::min(static_cast<int>(Mods::Mod::Count), kMaxMods);
    for (int i = 0; i < count; i++) {
        const auto mod = static_cast<Mods::Mod>(i);
        std::strncpy(entries[i].label, Mods::description(mod), sizeof(entries[i].label) - 1);
        entries[i].on = Mods::enabled(mod);
        if (mod == Mods::Mod::Cappy || mod == Mods::Mod::OdysseyJumpDive) entries[i].needs = static_cast<int>(Mods::Mod::OdysseyMovement);
    }
    instance().setMods(entries, count);
}

// The Odyssey camera page, from the camera settings in effect now.
void refreshCamera() {
    CameraOptions options;
    options.on = CameraSettings::enabled();
    options.speed = CameraSettings::speed();
    options.invertX = CameraSettings::invertX();
    options.invertY = CameraSettings::invertY();
    options.photo = CameraSettings::photoEnabled();
    instance().setCamera(options);
}

// Applies and saves changes made on the Odyssey camera page this frame.
void applyCameraChange() {
    CameraOptions options;
    if (!instance().takeCameraChange(&options)) {
        return;
    }
    CameraSettings::setEnabled(options.on);
    CameraSettings::setSpeed(options.speed);
    CameraSettings::setInvertX(options.invertX);
    CameraSettings::setInvertY(options.invertY);
    CameraSettings::setPhotoEnabled(options.photo);
    std::string error;
    if (!CameraSettings::save(&error)) {
        std::fprintf(stderr, "petari: camera: %s\n", error.c_str());
    }
}

// The Mod folder page: the disc-file mods found, with their saved states.
void refreshFolderMods() {
    FolderModEntry entries[kMaxFolderMods];
    const auto mods = Platform::ModFolder::detected();
    const auto states = Platform::ModFolder::detectedStates();
    const int count = std::min(static_cast<int>(mods.size()), kMaxFolderMods);
    for (int i = 0; i < count; i++) {
        const auto& mod = mods[i];
        std::snprintf(entries[i].label, sizeof(entries[i].label), "%s (%u files)", mod.title.c_str(), mod.fileCount);
        const auto state = states.find(mod.name);
        entries[i].on = state != states.end() && state->second;
    }
    instance().setFolderMods(entries, count);
}

// Saves the Mod folder toggles made this frame to mods.txt.
void applyFolderToggles() {
    const std::uint64_t toggles = instance().takeFolderToggles();
    if (toggles == 0) {
        return;
    }
    const auto mods = Platform::ModFolder::detected();
    for (int i = 0; i < static_cast<int>(mods.size()) && i < kMaxFolderMods; i++) {
        if (toggles & (std::uint64_t{1} << i)) {
            std::string error;
            if (!Platform::ModFolder::setEnabled(mods[i].name, instance().folderMods()[i].on, &error)) {
                std::fprintf(stderr, "petari: mod folder: %s\n", error.c_str());
            }
        }
    }
}

// Applies and saves toggles made on the Mods page this frame.
void applyModToggles() {
    const std::uint32_t toggles = instance().takeModToggles();
    if (toggles == 0) {
        return;
    }
    for (int i = 0; i < static_cast<int>(Mods::Mod::Count) && i < kMaxMods; i++) {
        if (toggles & (1u << i)) {
            Mods::setEnabled(static_cast<Mods::Mod>(i), instance().mods()[i].on);
        }
    }
    std::string error;
    if (!Mods::save(&error)) {
        std::fprintf(stderr, "petari: mods: %s\n", error.c_str());
    }
}
#endif

void init() {
    for (std::uint32_t& hold : gStickHold) {
        hold = 0;
    }
#ifdef PETARI_HOME_MENU_INPUT
    Input::takeMenuScroll();  // scrolling from before the menu opened is not for it
    instance().setControlsBackend(&gInputControls);
    refreshControls();
    refreshMods();
    refreshFolderMods();
    refreshCamera();
#endif
#ifdef PETARI_HOME_MENU_INPUT
    instance().setEditorEnabled(Mods::enabled(Mods::Mod::LevelEditor));
#endif
    instance().open();
}

void calc(const HBMControllerData* controllers) {
    Menu& menu = instance();
    FrameInput input = frameInput(controllers);
#ifdef PETARI_HOME_MENU_INPUT
    const Input::MenuScroll scroll = Input::takeMenuScroll();
    input.scrollRows = -scroll.rows;  // the wheel's + is toward the top
    input.scrollPages = scroll.pages;
#endif
    menu.update(input);
#ifdef PETARI_HOME_MENU_INPUT
    applyModToggles();
    menu.setEditorEnabled(Mods::enabled(Mods::Mod::LevelEditor));
    applyFolderToggles();
    applyCameraChange();
#endif
    // Level Select's Go, once the menu has closed: the game starts the warp
    // at its next normal gameplay frame (GameScene::update).
    const char* stage = nullptr;
    int mission = 0;
    if (menu.takeLevelRequest(&stage, &mission)) {
        std::snprintf(PlayerLaunch::warpStage, sizeof(PlayerLaunch::warpStage), "%s", stage);
        PlayerLaunch::warpScenario = mission;
        PlayerLaunch::warpPending.store(true);
    }
    if (menu.takeNewGalaxyRequest()) PlayerLaunch::newGalaxyPending.store(true);  // M4: host UI request.
    if (menu.selection() != Selection::None) {
        // HomeButtonLayout hides itself this frame and stops calling draw.
        publish(View{});
    }
}

void draw() {
    publish(instance().view());
}

HBMSelectBtnNum getSelectBtnNum() {
    return static_cast<HBMSelectBtnNum>(static_cast<int>(instance().selection()));
}

void setAdjustFlag(int flag) {
    instance().setWidescreen(flag != 0);
}

void startBlackOut() {
    instance().startBlackOut();
}

}  // namespace Hbm

}  // namespace PetariNative::HomeMenu
