#include "petari/home_menu.hpp"
#include "petari/launch_stage.hpp"
#include "petari/progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace PetariNative::HomeMenu {

namespace {

constexpr float kDimOpacity = 0.5f;

// Layout in KPAD space before the widescreen correction.
constexpr float kPanelHalfWidth = 0.5f;
constexpr float kPanelTop = -0.66f;
constexpr float kPanelBottomMargin = 0.06f;  // below the last item
constexpr float kItemHalfWidth = 0.44f;
constexpr float kItemTop = -0.3f;
constexpr float kItemHeight = 0.19f;
constexpr float kItemSpacing = 0.24f;

// The Controls page: a larger panel, the lines, and Back at the bottom.
constexpr float kControlsHalfWidth = 0.86f;
constexpr float kControlsTop = -0.92f;
constexpr float kControlsTitleY = -0.82f;
constexpr float kControlsMessageY = -0.73f;
constexpr float kControlsLinesTop = -0.66f;
constexpr float kControlsLinesBottom = 0.62f;
constexpr float kControlsLinesHalfWidth = 0.8f;
constexpr float kControlsBackTop = 0.68f;
constexpr float kControlsBackHeight = 0.15f;
constexpr float kControlsBackHalfWidth = 0.2f;

// The remapper: the table, then its buttons.
constexpr float kRemapTop = -0.62f;
constexpr float kRemapBottom = 0.58f;
constexpr float kRemapHalfWidth = 0.82f;   // the table's left edge (and the scrollbar's right)
constexpr float kRemapTableRight = 0.75f;  // the table's right edge; the scrollbar is beyond it
constexpr float kRemapLabelShare = 0.32f;  // of the table width; four slots share the rest
constexpr float kRemapButtonsTop = 0.65f;

constexpr const char* kListLabels[] = {"Resume", "Controls", "Mods", "My Progress", "Restart from Title", "Quit"};
constexpr int kListCount = 6;
constexpr int kListResume = 0;
constexpr int kListControls = 1;
constexpr int kListMods = 2;
constexpr int kListProgress = 3;
constexpr int kListRestart = 4;
constexpr int kListQuit = 5;
constexpr int kConfirmAccept = 0;
constexpr int kConfirmCancel = 1;
// Every other page's item count; View::items has to hold the largest.
constexpr int kProgressItems = 4;
constexpr int kCameraItems = 6;
constexpr int kLevelItems = 4;
constexpr int kFolderMaxItems = kFolderPageSize + 2;  // the page's toggles, Next page, Back
static_assert(kMaxItems >= kListCount, "View::items must hold the pause list");
static_assert(kMaxItems >= kProgressItems && kMaxItems >= kCameraItems && kMaxItems >= kLevelItems, "View::items too small");
static_assert(kMaxItems >= kFolderMaxItems, "View::items must hold a Mod folder page");
static_assert(kMaxItems >= 4, "the remapper's buttons");
static_assert(kMaxControls >= 8, "the progress page's lines");

// Reports a clamped out-of-range write to View::items / View::lines once per process.
void overflowOnce(const char* what, int index, int limit) {
    static std::once_flag once;
    std::call_once(once, [&] {
        std::fprintf(stderr, "petari: home menu: %s index %d past its capacity %d; clamped\n", what, index, limit);
    });
}

float progress(float time, float length) {
    return std::clamp(time / length, 0.0f, 1.0f);
}

}  // namespace

void Menu::create(const Config& config) {
    mConfig = config;
    if (!(mConfig.frameDelta > 0.0f)) {
        mConfig.frameDelta = 1.0f;
    }
    if (!(mConfig.widescreenAdjustX > 0.0f)) {
        mConfig.widescreenAdjustX = 1.0f;
    }
    mCreated = true;
    mPhase = Phase::Closed;
    mResult = Selection::None;
}

void Menu::setWidescreen(bool widescreen) {
    mWidescreen = widescreen;
}

void Menu::open() {
    if (!mCreated) {
        return;
    }
    mPhase = Phase::Opening;
    mResult = Selection::None;
    mConfirming = Selection::None;
    mLevelPending = false;
    mNewGalaxyPending = false;
    mFocus = kListResume;
    mTime = 0.0f;
    mBlackStart = 0.0f;
    mPanelStart = 0.0f;
    mRepeatDir = 0;
    mRepeatTime = 0.0f;
    mPointerWasValid = false;
    if (mRemapMode == RemapMode::Capturing && mBackend != nullptr) {
        mBackend->cancelCapture();
    }
    mRemapMode = RemapMode::Browse;
    play(Sound::HomeButton);
}

void Menu::update(const FrameInput& input) {
    const float dt = mConfig.frameDelta;
    const bool pointerMoved = input.pointerValid &&
                              (!mPointerWasValid || std::fabs(input.pointerX - mPointerX) > 1e-4f ||
                               std::fabs(input.pointerY - mPointerY) > 1e-4f);
    mPointerWasValid = input.pointerValid;
    if (input.pointerValid) {
        mPointerX = input.pointerX;
        mPointerY = input.pointerY;
    }

    switch (mPhase) {
    case Phase::Closed:
    case Phase::Finished:
        return;
    case Phase::Opening:
        mTime += dt;
        if (mTime >= kFadeFrames) {
            mPhase = Phase::List;
            mTime = 0.0f;
        }
        return;
    case Phase::Closing:
        mTime += dt;
        if (mTime >= kFadeFrames) {
            finish();
        }
        return;
    case Phase::BlackOut:
        mTime += dt;
        if (mTime >= kBlackOutFrames) {
            finish();
        }
        return;
    case Phase::List:
    case Phase::Confirm:
    case Phase::Controls:
    case Phase::Mods:
    case Phase::Levels:
    case Phase::Camera:
    case Phase::ModFolder:
    case Phase::Progress:
        break;
    }

    if (mPhase == Phase::Controls && mBackend != nullptr) {
        updateRemap(input, pointerMoved);
        return;
    }

    const int hovered = input.pointerValid ? itemAt(input.pointerX, input.pointerY) : -1;

    if (input.trigger & Button::Home) {
        play(Sound::ReturnApp);
        close();
        return;
    }
    // Escape presses Plus and B together; Plus alone decides that it is a back.
    if (input.trigger & Button::Plus) {
        back();
        return;
    }
    if (input.trigger & Button::A) {
        activate(mFocus);
        return;
    }
    if (input.trigger & Button::B) {
        if (hovered >= 0) {
            setFocus(hovered, false);
            activate(hovered);
        } else {
            back();
        }
        return;
    }

    if (pointerMoved && hovered >= 0) {
        setFocus(hovered, true);
    }

    if (mPhase == Phase::Camera && mFocus == 1) {
        const std::uint32_t side = input.trigger & (Button::Left | Button::Right);
        if (side == Button::Left || side == Button::Right) {
            changeCameraValue(mFocus, side == Button::Left ? -1 : 1);
            return;
        }
    }
    if (mPhase == Phase::Progress && mFocus == 0) {
        const std::uint32_t side = input.trigger & (Button::Left | Button::Right);
        if (side == Button::Left || side == Button::Right) {
            changeProgressGalaxy(side == Button::Left ? -1 : 1);
            return;
        }
    }
    if (mPhase == Phase::Levels && (mFocus == 0 || mFocus == 1)) {
        const std::uint32_t side = input.trigger & (Button::Left | Button::Right);
        if (side == Button::Left || side == Button::Right) {
            changeLevelValue(mFocus, side == Button::Left ? -1 : 1);
            return;
        }
    }

    const int steps = verticalStep(input);
    for (int i = 0; i < std::abs(steps); i++) {
        moveFocus(steps < 0 ? -1 : 1);
    }
}

// Up/Down this frame as focus steps (negative: up), with hold repeat.
int Menu::verticalStep(const FrameInput& input) {
    std::uint32_t dir = input.hold & (Button::Up | Button::Down);
    if (dir == (Button::Up | Button::Down)) {
        dir = 0;
    }
    int steps = 0;
    if (dir != 0 && (input.trigger & dir)) {
        mRepeatDir = dir;
        mRepeatTime = 0.0f;
        steps = 1;
    } else if (dir != 0 && dir == mRepeatDir) {
        mRepeatTime += mConfig.frameDelta;
        while (mRepeatTime >= kRepeatDelayFrames) {
            steps++;
            mRepeatTime -= kRepeatIntervalFrames;
        }
    } else {
        // Held since before the menu took it (or released): no repeat
        // without a fresh press.
        mRepeatDir = 0;
        mRepeatTime = 0.0f;
    }
    return dir == Button::Up ? -steps : steps;
}

void Menu::setControls(const ControlsEntry* entries, int count) {
    mControlCount = std::clamp(count, 0, kMaxControls);
    for (int i = 0; i < mControlCount; i++) {
        mControls[i] = entries[i];
        // Always terminated, even if the caller filled every byte.
        mControls[i].action[sizeof(mControls[i].action) - 1] = '\0';
        mControls[i].inputs[sizeof(mControls[i].inputs) - 1] = '\0';
        mControls[i].pad[sizeof(mControls[i].pad) - 1] = '\0';
    }
}

void Menu::setFooterKeys(const char* confirm, const char* back, const char* close, const char* change) {
    const char* names[4] = {confirm, back, close, change};
    for (int i = 0; i < 4; i++) {
        std::snprintf(mFooterKeys[i], sizeof(mFooterKeys[i]), "%s", names[i] != nullptr ? names[i] : "");
    }
}

void Menu::setMods(const ModsEntry* entries, int count) {
    mModCount = std::clamp(count, 0, kMaxMods);
    for (int i = 0; i < mModCount; i++) {
        mMods[i] = entries[i];
        mMods[i].label[sizeof(mMods[i].label) - 1] = '\0';
    }
}

// "Cappy: On", or "Cappy: On (needs Odyssey movement)" while a mod it needs is off.
void Menu::refreshModLabels() {
    for (int i = 0; i < mModCount; i++) {
        const int needs = mMods[i].needs;
        const bool missing = mMods[i].on && needs >= 0 && needs < mModCount && !mMods[needs].on;
        std::snprintf(mModLabels[i], sizeof(mModLabels[i]), "%s: %s%s%s%s", mMods[i].label, mMods[i].on ? "On" : "Off",
                      missing ? " (needs " : "", missing ? mMods[needs].label : "", missing ? ")" : "");
    }
}

void Menu::setCamera(const CameraOptions& options) {
    mCamera = options;
    mCamera.speed = std::clamp(mCamera.speed, 1, 5);
    mCameraChanged = false;
    refreshCameraLabels();
}

bool Menu::takeCameraChange(CameraOptions* options) {
    if (!mCameraChanged) {
        return false;
    }
    mCameraChanged = false;
    *options = mCamera;
    return true;
}

void Menu::refreshCameraLabels() {
    std::snprintf(mCameraLabels[0], sizeof(mCameraLabels[0]), "Odyssey camera: %s", mCamera.on ? "On" : "Off");
    std::snprintf(mCameraLabels[1], sizeof(mCameraLabels[1]), "Speed: %d", mCamera.speed);
    std::snprintf(mCameraLabels[2], sizeof(mCameraLabels[2]), "Invert horizontal: %s", mCamera.invertX ? "On" : "Off");
    std::snprintf(mCameraLabels[3], sizeof(mCameraLabels[3]), "Invert vertical: %s", mCamera.invertY ? "On" : "Off");
    std::snprintf(mCameraLabels[4], sizeof(mCameraLabels[4]), "Photo mode (P): %s", mCamera.photo ? "On" : "Off");
}

void Menu::changeCameraValue(int item, int delta) {
    switch (item) {
    case 0: mCamera.on = !mCamera.on; break;
    case 1: mCamera.speed = (mCamera.speed - 1 + delta + 5) % 5 + 1; break;
    case 2: mCamera.invertX = !mCamera.invertX; break;
    case 3: mCamera.invertY = !mCamera.invertY; break;
    case 4: mCamera.photo = !mCamera.photo; break;
    default: return;
    }
    mCameraChanged = true;
    refreshCameraLabels();
    play(item == 1 ? Sound::Focus : Sound::Select);
}

void Menu::setFolderMods(const FolderModEntry* entries, int count) {
    mFolderPage = true;
    mFolderCount = std::clamp(count, 0, kMaxFolderMods);
    for (int i = 0; i < mFolderCount; i++) {
        mFolderMods[i] = entries[i];
        mFolderMods[i].label[sizeof(mFolderMods[i].label) - 1] = '\0';
    }
    mFolderPageIndex = std::min(mFolderPageIndex, std::max(0, folderPages() - 1));
}

std::uint64_t Menu::takeFolderToggles() {
    const std::uint64_t toggles = mFolderToggles;
    mFolderToggles = 0;
    return toggles;
}

void Menu::refreshFolderLabels() {
    for (int i = 0; i < folderShown(); i++) {
        const FolderModEntry& mod = mFolderMods[mFolderPageIndex * kFolderPageSize + i];  // audit-ok: i < folderShown(), which stops at mFolderCount <= kMaxFolderMods
        std::snprintf(mFolderLabels[i], sizeof(mFolderLabels[i]), "%s: %s", mod.label, mod.on ? "On" : "Off");
    }
    if (mFolderCount == 0) {
        std::snprintf(mFolderMessage, sizeof(mFolderMessage), "No mods found. Put mod folders in mods/ in the user folder.");
    } else {
        std::snprintf(mFolderMessage, sizeof(mFolderMessage), "Replace disc files. Page %d of %d. Applies after restarting the game.",
                      mFolderPageIndex + 1, std::max(1, folderPages()));
    }
}

namespace {
const char* missionKind(const App::LaunchStage::Galaxy& galaxy, int mission) {
    const char digit = static_cast<char>('0' + mission);
    if (std::strchr(galaxy.comets, digit) != nullptr) return " (comet)";
    if (std::strchr(galaxy.hidden, digit) != nullptr) return " (hidden star)";
    return "";
}

void formatTime(double seconds, char* out, size_t size) {
    const int whole = static_cast<int>(seconds);
    std::snprintf(out, size, "%d:%02d.%02d", whole / 60, whole % 60, static_cast<int>((seconds - whole) * 100.0 + 0.5) % 100);
}
}  // namespace

void Menu::refreshProgress() {
    int count = 0;
    const App::LaunchStage::Galaxy* galaxies = App::LaunchStage::galaxies(&count);
    mProgressGalaxy = count > 0 ? std::clamp(mProgressGalaxy, 0, count - 1) : 0;
    mProgressLineCount = 0;
    if (count == 0) {
        std::snprintf(mProgressLabels[0], sizeof(mProgressLabels[0]), "No galaxies");
        return;
    }
    const App::LaunchStage::Galaxy& galaxy = galaxies[mProgressGalaxy];
    int clearedHere = 0;
    ControlsEntry& header = mProgressLines[mProgressLineCount++];
    std::memset(&header, 0, sizeof(header));
    std::snprintf(header.inputs, sizeof(header.inputs), "Best time / deaths / coins / Star Bits");
    std::snprintf(header.pad, sizeof(header.pad), "Clears, first clear");
    for (int mission = 1; mission <= galaxy.missions && mProgressLineCount < kMaxProgressLines; mission++) {
        ControlsEntry& line = mProgressLines[mProgressLineCount++];
        std::memset(&line, 0, sizeof(line));
        std::snprintf(line.action, sizeof(line.action), "Mission %d%s", mission, missionKind(galaxy, mission));
        Progress::Mission record;
        if (Progress::find(galaxy.stage, mission, &record)) {
            clearedHere++;
            char time[24];
            formatTime(record.bestTimeS, time, sizeof(time));
            std::snprintf(line.inputs, sizeof(line.inputs), "Cleared %s  %d  %d  %d", time, record.fewestDeaths, record.bestCoins,
                          record.bestStarBits);
            std::snprintf(line.pad, sizeof(line.pad), "x%d  %.10s", record.clears, record.firstClear.c_str());
        } else {
            std::snprintf(line.inputs, sizeof(line.inputs), "Not cleared yet");
        }
    }
    std::snprintf(mProgressLabels[0], sizeof(mProgressLabels[0]), "<  %s  >", galaxy.name);
    std::snprintf(mProgressLabels[1], sizeof(mProgressLabels[1]), mProgressResetArmed ? "Press A again to erase everything" : "Reset progress...");
    std::snprintf(mProgressLabels[2], sizeof(mProgressLabels[2]), "Badges: %s", Progress::badgesEnabled() ? "On" : "Off");
    std::snprintf(mProgressLabels[3], sizeof(mProgressLabels[3]), "Back");
    std::snprintf(mProgressMessage, sizeof(mProgressMessage), "%s: %d of %d cleared here, %d in all. Your save is not changed.",
                  App::LaunchStage::domeName(galaxy.dome), clearedHere, galaxy.missions, Progress::clearedCount());
}

void Menu::changeProgressGalaxy(int delta) {
    int count = 0;
    App::LaunchStage::galaxies(&count);
    if (count > 0) {
        mProgressGalaxy = (mProgressGalaxy + delta + count) % count;
    }
    mProgressResetArmed = false;
    refreshProgress();
    play(Sound::Focus);
}

bool Menu::takeLevelRequest(const char** stage, int* mission) {
    if (!mLevelPending || mPhase != Phase::Finished) {
        return false;
    }
    mLevelPending = false;
    int count = 0;
    const App::LaunchStage::Galaxy* galaxies = App::LaunchStage::galaxies(&count);
    *stage = galaxies[mLevelGalaxy].stage;
    *mission = mLevelMission;
    return true;
}

void Menu::setEditorEnabled(bool enabled) {
    if (enabled == mEditorEnabled) return;
    mEditorEnabled = enabled;
    int count = 0;
    App::LaunchStage::galaxies(&count);
    if (!enabled && mLevelGalaxy >= count) mLevelGalaxy = 0;
    if (!enabled) mNewGalaxyPending = false;
    if (mPhase == Phase::Levels) refreshLevelLabels();
}

bool Menu::takeNewGalaxyRequest() {
    if (!mEditorEnabled || !mNewGalaxyPending || mPhase != Phase::Finished) return false;
    mNewGalaxyPending = false;
    return true;
}

void Menu::refreshLevelLabels() {
    int count = 0;
    const auto* galaxies = App::LaunchStage::galaxies(&count);
    if (mEditorEnabled && mLevelGalaxy == count) {
        std::snprintf(mLevelLabels[0], sizeof(mLevelLabels[0]), "+ New galaxy...");
        std::snprintf(mLevelLabels[1], sizeof(mLevelLabels[1]), "Choose a template in the editor");
        std::snprintf(mLevelMessage, sizeof(mLevelMessage), "Mod galaxies. Create your own galaxy.");
        return;
    }
    const App::LaunchStage::Galaxy& galaxy = galaxies[mLevelGalaxy];
    const char digit = static_cast<char>('0' + mLevelMission);
    const bool comet = std::strchr(galaxy.comets, digit) != nullptr;
    const bool hidden = std::strchr(galaxy.hidden, digit) != nullptr;
    std::snprintf(mLevelLabels[0], sizeof(mLevelLabels[0]), "%s", galaxy.name);
    std::snprintf(mLevelLabels[1], sizeof(mLevelLabels[1]), "Mission %d of %d%s", mLevelMission, galaxy.missions,
                  comet ? " (comet)" : hidden ? " (hidden star)" : "");
    std::snprintf(mLevelMessage, sizeof(mLevelMessage), "%s. Left/Right changes; your save is not changed.",
                  App::LaunchStage::domeName(galaxy.dome));
}

void Menu::changeLevelValue(int item, int delta) {
    int count = 0;
    const App::LaunchStage::Galaxy* galaxies = App::LaunchStage::galaxies(&count);
    if (item == 0) {
        const int slots = count + (mEditorEnabled ? 1 : 0);
        mLevelGalaxy = (mLevelGalaxy + delta + slots) % slots;
        mLevelMission = 1;
    } else if (mLevelGalaxy < count) {
        const int missions = galaxies[mLevelGalaxy].missions;
        mLevelMission = (mLevelMission - 1 + delta + missions) % missions + 1;
    }
    refreshLevelLabels();
    play(Sound::Focus);
}

std::uint32_t Menu::takeModToggles() {
    const std::uint32_t toggles = mModToggles;
    mModToggles = 0;
    return toggles;
}

void Menu::startBlackOut() {
    switch (mPhase) {
    case Phase::Opening:
    case Phase::List:
    case Phase::Confirm:
    case Phase::Controls:
    case Phase::Mods:
    case Phase::Levels:
    case Phase::Camera:
    case Phase::ModFolder:
    case Phase::Progress:
    case Phase::Closing:
        blackOut(Selection::Restart);
        break;
    case Phase::BlackOut:
        // Already fading after Restart or Quit: the console reset takes over,
        // and HomeButtonLayout expects the reset result.
        mResult = Selection::Restart;
        break;
    case Phase::Closed:
    case Phase::Finished:
        break;
    }
}

View Menu::view() const {
    View view;
    view.phase = mPhase;
    // Every write into View::items goes through here: past the end it is dropped (and logged
    // once) rather than landing on View::lineCount and the arrays after it.
    auto setItem = [&view](int index, const char* label) {
        if (index < 0 || index >= kMaxItems) {
            overflowOnce("item", index, kMaxItems);
            return;
        }
        view.items[index].label = label;
    };
    auto copyLines = [&view](const ControlsEntry* source, int count) {
        if (count > kMaxControls) {
            overflowOnce("line", count, kMaxControls);
        }
        view.lineCount = std::clamp(count, 0, kMaxControls);
        for (int i = 0; i < view.lineCount; i++) {
            view.lines[i] = source[i];
        }
    };
    switch (mPhase) {
    case Phase::Closed:
    case Phase::Finished:
        return view;
    case Phase::Opening:
        view.panelOpacity = progress(mTime, kFadeFrames);
        break;
    case Phase::List:
    case Phase::Confirm:
    case Phase::Controls:
    case Phase::Mods:
    case Phase::Levels:
    case Phase::Camera:
    case Phase::ModFolder:
    case Phase::Progress:
        view.panelOpacity = 1.0f;
        break;
    case Phase::Closing:
        view.panelOpacity = mPanelStart * (1.0f - progress(mTime, kFadeFrames));
        break;
    case Phase::BlackOut:
        view.panelOpacity = mPanelStart;
        view.blackOpacity = mBlackStart + (1.0f - mBlackStart) * progress(mTime, kBlackOutFrames);
        break;
    }
    view.visible = true;
    view.dimOpacity = kDimOpacity * view.panelOpacity;

    const float ax = adjustX();

    const bool confirm = mConfirming != Selection::None;
    const bool controls = mPhase == Phase::Controls;
    const bool progressPage = mPhase == Phase::Progress;
    if (controls && mBackend != nullptr) {
        view.title = "Controls";
        view.message = mRemapMessage[0] != '\0' ? mRemapMessage
                                                : "Choose a slot and press " PETARI_ICON_A " to change it.";
        RemapView& remap = view.remap;
        remap.active = true;
        remap.totalRows = mRemapRowCount;
        remap.firstRow = mRemapTop;
        remap.rowCount = std::clamp(mRemapRowCount - mRemapTop, 0, kRemapVisibleRows);
        for (int i = 0; i < remap.rowCount; i++) {
            remap.rows[i] = mRemapRows[mRemapTop + i];  // audit-ok: i < rowCount = clamp(mRemapRowCount - mRemapTop, 0, kRemapVisibleRows)
        }
        const bool rowShown = mRemapRow >= mRemapTop && mRemapRow < mRemapTop + remap.rowCount;
        remap.focusRow = !mRemapOnButtons && rowShown ? mRemapRow - mRemapTop : -1;
        remap.focusSlot = mRemapSlot;
        remap.selectedRow = rowShown ? mRemapRow - mRemapTop : -1;
        remap.selectedSlot = mRemapSlot;
        remap.capturing = mRemapMode == RemapMode::Capturing;
        remap.area = remapArea();
        for (int r = 0; r < remap.rowCount; r++) {
            for (int slot = 0; slot < kRemapSlots; slot++) {
                remap.cells[r][slot] = remapCellRect(r, slot);
            }
        }
        scrollbarRects(&remap.upArrow, &remap.downArrow, &remap.track, &remap.thumb);
        remap.dragging = mThumbDragging;
        if (mRemapMode == RemapMode::Conflict) {
            setItem(0, "Swap");
            setItem(1, "Keep both");
            setItem(2, "Cancel");
        } else {
            setItem(0, "Clear slot");
            setItem(1, "Reset action");
            setItem(2, mResetAllArmed ? "A again: reset all" : "Reset all");
            setItem(3, "Back");
        }
    } else if (controls) {
        view.title = "Controls";
        view.message = "Change them in controls.txt in the game's user folder.";
        setItem(0, "Back");
        copyLines(mControls, mControlCount);
        view.linesArea = {-kControlsLinesHalfWidth / ax, kControlsLinesTop, kControlsLinesHalfWidth / ax,
                          kControlsLinesBottom};
    } else if (mPhase == Phase::Progress) {
        view.title = "My Progress";
        view.message = mProgressMessage;
        for (int i = 0; i < kProgressItems; i++) {
            setItem(i, mProgressLabels[i]);
        }
        copyLines(mProgressLines, mProgressLineCount);
        view.linesArea = {-kControlsLinesHalfWidth / ax, kControlsLinesTop + 0.2f, kControlsLinesHalfWidth / ax, 0.42f};
    } else if (mPhase == Phase::Mods) {
        view.title = "Mods";
        view.message = "Off by default. Saved in mods.txt; change their keys on the Controls page.";
        for (int i = 0; i < mModCount; i++) {
            setItem(i, mModLabels[i]);  // audit-ok: i < mModCount <= kMaxMods (setMods clamps)
        }
        setItem(mModCount, "Odyssey camera...");
        setItem(mModCount + 1, "Level Select");
        if (mFolderPage) {
            setItem(mModCount + 2, "Mod folder...");
        }
        setItem(modsBackIndex(), "Back");
    } else if (mPhase == Phase::ModFolder) {
        view.title = "Mod folder";
        view.message = mFolderMessage;
        const int shown = folderShown();
        for (int i = 0; i < shown; i++) {
            setItem(i, mFolderLabels[i]);
        }
        if (folderPages() > 1) {
            setItem(shown, "Next page");
        }
        setItem(shown + (folderPages() > 1 ? 1 : 0), "Back");
    } else if (mPhase == Phase::Camera) {
        view.title = "Odyssey camera";
        view.message = "Right stick or middle-drag orbits; wheel or Z/X zooms; C recentres. P: photo mode.";
        for (int i = 0; i < 5; i++) {
            setItem(i, mCameraLabels[i]);
        }
        setItem(5, "Back");
    } else if (mPhase == Phase::Levels) {
        view.title = "Level Select";
        view.message = mLevelMessage;
        setItem(0, mLevelLabels[0]);
        setItem(1, mLevelLabels[1]);
        int count = 0;
        App::LaunchStage::galaxies(&count);
        setItem(2, mEditorEnabled && mLevelGalaxy == count ? "Create..." : "Go");
        setItem(3, "Back");
    } else if (!confirm) {
        view.bands = true;
        view.title = "Super Mario Galaxy";
        view.message = "Paused";
        for (int i = 0; i < kListCount; i++) {
            setItem(i, kListLabels[i]);
        }
    } else {
        view.bands = true;
        const bool restart = mConfirming == Selection::Restart;
        view.title = restart ? "Restart from the title screen?" : "Quit Super Mario Galaxy?";
        view.message = "Progress since your last save will be lost.";
        setItem(kConfirmAccept, restart ? "Restart" : "Quit");
        setItem(kConfirmCancel, "Cancel");
    }
    // "<icon> <key>: <what>" for each button that works here.
    const bool changes = mPhase == Phase::Levels || mPhase == Phase::Camera || progressPage;
    size_t used = 0;
    auto hint = [&](const char* icon, const char* key, const char* what) {
        if (used >= sizeof(view.footer)) return;
        const int n = std::snprintf(view.footer + used, sizeof(view.footer) - used, "%s%s %s%s%s", used > 0 ? "     " : "", icon, key,
                                    key[0] != '\0' ? ": " : "", what);
        used += n > 0 ? static_cast<size_t>(n) : 0;
    };
    if (changes) hint(PETARI_ICON_PAD, mFooterKeys[3], "change");
    hint(PETARI_ICON_A, mFooterKeys[0], "choose");
    hint(PETARI_ICON_PLUS, mFooterKeys[1], "back");
    hint(PETARI_ICON_HOME, mFooterKeys[2], "close");
    view.itemCount = std::clamp(itemCount(), 0, kMaxItems);
    if (controls || progressPage) {
        view.panel = {-kControlsHalfWidth / ax, kControlsTop, kControlsHalfWidth / ax,
                      itemRect(progressPage ? 3 : 0).y1 + kPanelBottomMargin};
        view.titleY = kControlsTitleY;
        view.messageY = controls && mBackend != nullptr ? -0.705f : kControlsMessageY;
    } else if (view.bands) {
        // The game's pause-screen layout: a full-width band across the top with the title
        // and message, the buttons over the scene below it.
        const float firstItemTop = itemRect(0).y0;
        view.panel = {-1.0f, -1.0f, 1.0f, firstItemTop - 0.08f};
        view.titleY = -1.0f + (view.panel.y1 + 1.0f) * 0.42f;
        view.messageY = -1.0f + (view.panel.y1 + 1.0f) * 0.75f;
    } else {
        // Room for the title and a two-line message above the first item.
        const float firstItemTop = itemRect(0).y0;
        const float panelTop = std::min(kPanelTop, firstItemTop - 0.34f);
        view.panel = {-kPanelHalfWidth / ax, panelTop, kPanelHalfWidth / ax,
                      itemRect(view.itemCount - 1).y1 + kPanelBottomMargin};
        view.titleY = panelTop + (firstItemTop - panelTop) * 0.33f;
        view.messageY = panelTop + (firstItemTop - panelTop) * 0.66f;
    }
    for (int i = 0; i < view.itemCount; i++) {
        view.items[i].rect = itemRect(i);
        view.items[i].focused = i == mFocus;
    }
    if (controls && mBackend != nullptr) {
        for (int i = 0; i < view.itemCount; i++) {
            view.items[i].focused = mRemapOnButtons && i == mRemapButton && mRemapMode != RemapMode::Capturing;
        }
    }
    return view;
}

int Menu::itemCount() const {
    return std::min(pageItemCount(), kMaxItems);
}

int Menu::pageItemCount() const {
    if (mPhase == Phase::Controls) {
        return mBackend == nullptr ? 1 : mRemapMode == RemapMode::Conflict ? 3 : 4;
    }
    if (mPhase == Phase::Mods) {
        return modsBackIndex() + 1;
    }
    if (mPhase == Phase::ModFolder) {
        return folderShown() + (folderPages() > 1 ? 1 : 0) + 1;
    }
    if (mPhase == Phase::Progress) {
        return kProgressItems;
    }
    if (mPhase == Phase::Camera) {
        return kCameraItems;
    }
    if (mPhase == Phase::Levels) {
        return kLevelItems;
    }
    return mConfirming != Selection::None ? 2 : kListCount;
}

Rect Menu::itemRect(int index) const {
    const float ax = adjustX();
    if (mPhase == Phase::Controls && mBackend != nullptr) {
        // A row of buttons under the table: four (Clear, Reset action, Reset all, Back), or three
        // for a conflict (Swap, Keep both, Cancel), centered.
        const int count = itemCount();
        const float left = -0.41f * 0.5f * static_cast<float>(count) + 0.02f + 0.41f * static_cast<float>(index);
        return {left / ax, kRemapButtonsTop, (left + 0.37f) / ax, kRemapButtonsTop + 0.14f};
    }
    if (mPhase == Phase::Controls) {
        return {-kControlsBackHalfWidth / ax, kControlsBackTop, kControlsBackHalfWidth / ax,
                kControlsBackTop + kControlsBackHeight};
    }
    if (mPhase == Phase::Progress) {
        // The galaxy selector under the message; Reset, Badges and Back side by side at the bottom.
        if (index == 0) return {-0.5f / ax, -0.64f, 0.5f / ax, -0.50f};
        const float left = -0.78f + 0.54f * static_cast<float>(index - 1);
        return {left / ax, 0.52f, (left + 0.5f) / ax, 0.67f};
    }
    // Six or more items (the Mods page) use a tighter column so they fit.
    const int count = itemCount();
    const bool compact = count > 5;
    const bool dense = count > 6;  // the Mods page with a Mod folder entry, or more mods
    // The dense column ends above the footer band (View::footer, from y 0.915). With many
    // mods the spacing shrinks to keep every row on screen.
    float spacing = dense ? 0.165f : compact ? 0.2f : kItemSpacing;
    float height = dense ? 0.135f : compact ? 0.16f : kItemHeight;
    if (dense) {
        constexpr float kDenseTop = -0.46f;
        constexpr float kDenseBottom = 0.855f;  // the panel adds kPanelBottomMargin below this
        constexpr float kHeightShare = 0.82f;
        const float fit = (kDenseBottom - kDenseTop) / (static_cast<float>(count - 1) + kHeightShare);
        if (fit < spacing) {
            spacing = fit;
            height = fit * kHeightShare;
        }
    }
    const float top = (dense ? -0.46f : compact ? -0.32f : kItemTop) + spacing * static_cast<float>(index);
    return {-kItemHalfWidth / ax, top, kItemHalfWidth / ax, top + height};
}

int Menu::itemAt(float x, float y) const {
    for (int i = 0; i < itemCount(); i++) {
        if (itemRect(i).contains(x, y)) {
            return i;
        }
    }
    return -1;
}

void Menu::play(int num) const {
    if (mConfig.sound != nullptr) {
        mConfig.sound(Sound::EventPlaySound, num);
    }
}

void Menu::event(int event) const {
    if (mConfig.sound != nullptr) {
        mConfig.sound(event, 0);
    }
}

void Menu::setFocus(int focus, bool withSound) {
    if (focus == mFocus) {
        return;
    }
    mFocus = focus;
    if (mPhase == Phase::Progress && mProgressResetArmed && focus != 1) {
        mProgressResetArmed = false;  // moving away disarms the erase
        refreshProgress();
    }
    if (withSound) {
        play(Sound::Focus);
    }
}

void Menu::moveFocus(int delta) {
    setFocus(std::clamp(mFocus + delta, 0, itemCount() - 1), true);
}

void Menu::activate(int index) {
    if (mPhase == Phase::List) {
        switch (index) {
        case kListResume:
            play(Sound::ReturnApp);
            close();
            break;
        case kListControls:
            play(Sound::Select);
            mPhase = Phase::Controls;
            mFocus = 0;
            mRepeatDir = 0;
            if (mBackend != nullptr) {
                refreshRemapRows();
                mRemapMode = RemapMode::Browse;
                mRemapRow = 0;
                mRemapSlot = 0;
                mRemapTop = 0;
                mRemapOnButtons = false;
                mResetAllArmed = false;
                mThumbDragging = false;
                mBackend->notice(mRemapMessage, sizeof(mRemapMessage));
            }
            break;
        case kListProgress:
            play(Sound::Select);
            mProgressResetArmed = false;
            refreshProgress();
            mPhase = Phase::Progress;
            mFocus = 0;
            mRepeatDir = 0;
            break;
        case kListMods:
            play(Sound::Select);
            refreshModLabels();
            mPhase = Phase::Mods;
            mFocus = 0;
            mRepeatDir = 0;
            break;
        case kListRestart:
        case kListQuit:
            play(Sound::Select);
            mConfirming = index == kListRestart ? Selection::Restart : Selection::Quit;
            mPhase = Phase::Confirm;
            mFocus = kConfirmCancel;
            mRepeatDir = 0;
            break;
        }
    } else if (mPhase == Phase::Controls) {
        play(Sound::Cancel);
        mPhase = Phase::List;
        mFocus = kListControls;
        mRepeatDir = 0;
    } else if (mPhase == Phase::Levels) {
        if (index == 0 || index == 1) {
            changeLevelValue(index, 1);
        } else if (index == 2) {
            play(Sound::Select);
            int count = 0;
            App::LaunchStage::galaxies(&count);
            mNewGalaxyPending = mEditorEnabled && mLevelGalaxy == count;
            mLevelPending = !mNewGalaxyPending;
            close();
        } else {
            play(Sound::Cancel);
            mPhase = Phase::Mods;
            mFocus = mModCount + 1;
            mRepeatDir = 0;
        }
    } else if (mPhase == Phase::Progress) {
        if (index == 0) {
            changeProgressGalaxy(1);
        } else if (index == 1) {
            if (!mProgressResetArmed) {
                play(Sound::Select);
                mProgressResetArmed = true;
            } else {
                play(Sound::Select);
                mProgressResetArmed = false;
                Progress::reset();
            }
            refreshProgress();
        } else if (index == 2) {
            play(Sound::Select);
            Progress::setBadgesEnabled(!Progress::badgesEnabled());
            refreshProgress();
        } else {
            play(Sound::Cancel);
            mProgressResetArmed = false;
            mPhase = Phase::List;
            mFocus = kListProgress;
            mRepeatDir = 0;
        }
    } else if (mPhase == Phase::ModFolder) {
        const int shown = folderShown();
        const bool paged = folderPages() > 1;
        if (index >= 0 && index < shown) {
            play(Sound::Select);
            const int entry = mFolderPageIndex * kFolderPageSize + index;
            mFolderMods[entry].on = !mFolderMods[entry].on;
            mFolderToggles |= std::uint64_t{1} << entry;
            refreshFolderLabels();
        } else if (paged && index == shown) {
            play(Sound::Select);
            mFolderPageIndex = (mFolderPageIndex + 1) % folderPages();
            refreshFolderLabels();
            mFocus = 0;
        } else {
            play(Sound::Cancel);
            mPhase = Phase::Mods;
            mFocus = mModCount + 2;
            mRepeatDir = 0;
        }
    } else if (mPhase == Phase::Camera) {
        if (index >= 0 && index < 5) {
            changeCameraValue(index, 1);
        } else {
            play(Sound::Cancel);
            mPhase = Phase::Mods;
            mFocus = mModCount;
            mRepeatDir = 0;
        }
    } else if (mPhase == Phase::Mods) {
        if (index == mModCount) {
            play(Sound::Select);
            refreshCameraLabels();
            mPhase = Phase::Camera;
            mFocus = 0;
            mRepeatDir = 0;
        } else if (index == mModCount + 1) {
            play(Sound::Select);
            refreshLevelLabels();
            mPhase = Phase::Levels;
            mFocus = 0;
            mRepeatDir = 0;
        } else if (mFolderPage && index == mModCount + 2) {
            play(Sound::Select);
            mFolderPageIndex = 0;
            refreshFolderLabels();
            mPhase = Phase::ModFolder;
            mFocus = 0;
            mRepeatDir = 0;
        } else if (index >= 0 && index < mModCount) {
            play(Sound::Select);
            mMods[index].on = !mMods[index].on;
            mModToggles |= 1u << index;
            refreshModLabels();  // a mod another one needs changes that one's line too
        } else {
            play(Sound::Cancel);
            mPhase = Phase::List;
            mFocus = kListMods;
            mRepeatDir = 0;
        }
    } else if (mPhase == Phase::Confirm) {
        if (index == kConfirmAccept) {
            play(mConfirming == Selection::Restart ? Sound::ResetApp : Sound::GotoMenu);
            blackOut(mConfirming);
        } else {
            play(Sound::Cancel);
            mFocus = mConfirming == Selection::Restart ? kListRestart : kListQuit;
            mConfirming = Selection::None;
            mPhase = Phase::List;
            mRepeatDir = 0;
        }
    }
}

void Menu::back() {
    if (mPhase == Phase::Confirm) {
        activate(kConfirmCancel);
    } else if (mPhase == Phase::Controls) {
        activate(0);
    } else if (mPhase == Phase::Mods) {
        activate(modsBackIndex());
    } else if (mPhase == Phase::ModFolder) {
        activate(itemCount() - 1);
    } else if (mPhase == Phase::Progress) {
        activate(3);
    } else if (mPhase == Phase::Levels) {
        activate(3);
    } else if (mPhase == Phase::Camera) {
        activate(5);
    } else {
        play(Sound::ReturnApp);
        close();
    }
}

void Menu::close() {
    if (mRemapMode == RemapMode::Capturing && mBackend != nullptr) {
        mBackend->cancelCapture();
    }
    mRemapMode = RemapMode::Browse;
    mPanelStart = view().panelOpacity;
    mPhase = Phase::Closing;
    mTime = 0.0f;
    mResult = Selection::Resume;
    event(Sound::EventBeginExitAnim);
}

void Menu::blackOut(Selection result) {
    if (mRemapMode == RemapMode::Capturing && mBackend != nullptr) {
        mBackend->cancelCapture();
    }
    mRemapMode = RemapMode::Browse;
    const View current = view();
    mPanelStart = current.panelOpacity;
    mBlackStart = current.blackOpacity;
    mPhase = Phase::BlackOut;
    mTime = 0.0f;
    mResult = result;
    event(Sound::EventBeginBlackOut);
}

void Menu::finish() {
    mPhase = Phase::Finished;
    event(Sound::EventEndMenu);
}

// --- The Controls page's remapper ---

void Menu::setControlsBackend(ControlsBackend* backend) {
    if (mRemapMode == RemapMode::Capturing && mBackend != nullptr) {
        mBackend->cancelCapture();
    }
    mBackend = backend;
    mRemapMode = RemapMode::Browse;
    mRemapRowCount = 0;
}

void Menu::refreshRemapRows() {
    mRemapRowCount = mBackend != nullptr ? std::clamp(mBackend->rowCount(), 0, kMaxRemapRows) : 0;
    for (int i = 0; i < mRemapRowCount; i++) {
        mRemapRows[i] = RemapRow{};
        mBackend->row(i, &mRemapRows[i]);
        mRemapRows[i].label[sizeof(mRemapRows[i].label) - 1] = '\0';
        for (auto& slot : mRemapRows[i].slots) {
            slot[sizeof(slot) - 1] = '\0';
        }
    }
    mRemapRow = std::clamp(mRemapRow, 0, std::max(0, mRemapRowCount - 1));
}

void Menu::remapMessage(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(mRemapMessage, sizeof(mRemapMessage), format, args);
    va_end(args);
}

void Menu::scrollToRow() {
    if (mRemapRow < mRemapTop) {
        mRemapTop = mRemapRow;
    } else if (mRemapRow >= mRemapTop + kRemapVisibleRows) {
        mRemapTop = mRemapRow - kRemapVisibleRows + 1;
    }
    mRemapTop = std::clamp(mRemapTop, 0, std::max(0, mRemapRowCount - kRemapVisibleRows));
}

Rect Menu::remapArea() const {
    const float ax = adjustX();
    return {-kRemapHalfWidth / ax, kRemapTop, kRemapTableRight / ax, kRemapBottom};
}

Rect Menu::remapPanel() const {
    const float ax = adjustX();
    return {-kControlsHalfWidth / ax, kControlsTop, kControlsHalfWidth / ax, kRemapButtonsTop + 0.14f + kPanelBottomMargin};
}

// The scrollbar beside the rows (below the header): an arrow at each end, the
// track between, and a thumb as tall as the shown fraction of the rows.
void Menu::scrollbarRects(Rect* up, Rect* down, Rect* track, Rect* thumb) const {
    const float ax = adjustX();
    const Rect area = remapArea();
    const float rowHeight = (area.y1 - area.y0) / static_cast<float>(kRemapVisibleRows + 1);
    const float x0 = (kRemapTableRight + 0.02f) / ax, x1 = (kRemapHalfWidth + 0.005f) / ax;
    const float top = area.y0 + rowHeight;
    *up = {x0, top, x1, top + rowHeight};
    *down = {x0, area.y1 - rowHeight, x1, area.y1};
    *track = {x0, up->y1, x1, down->y0};
    const int total = std::max(mRemapRowCount, 1);
    const float height = track->y1 - track->y0;
    const float shown = std::min(1.0f, static_cast<float>(kRemapVisibleRows) / static_cast<float>(total));
    const float thumbHeight = std::max(height * shown, rowHeight * 0.6f);
    const int range = std::max(0, mRemapRowCount - kRemapVisibleRows);
    const float at = range > 0 ? static_cast<float>(mRemapTop) / static_cast<float>(range) : 0.0f;
    const float thumbTop = track->y0 + (height - thumbHeight) * at;
    *thumb = {x0, thumbTop, x1, thumbTop + thumbHeight};
}

// Scrolls the list by rows. moveFocus (paging): the focus moves as far; else
// it stays where it is unless it would leave the shown rows.
void Menu::scrollRemapBy(int rows, bool moveFocus) {
    const int range = std::max(0, mRemapRowCount - kRemapVisibleRows);
    const int before = mRemapTop;
    mRemapTop = std::clamp(mRemapTop + rows, 0, range);
    if (moveFocus) {
        mRemapRow = std::clamp(mRemapRow + rows, 0, std::max(0, mRemapRowCount - 1));
        mRemapOnButtons = false;
    }
    mRemapRow = std::clamp(mRemapRow, mRemapTop, std::max(mRemapTop, std::min(mRemapRowCount, mRemapTop + kRemapVisibleRows) - 1));
    if (mRemapTop != before || moveFocus) {
        mResetAllArmed = false;
        play(Sound::Focus);
    }
}

// A slot of a shown row; the header takes the table's first row.
Rect Menu::remapCellRect(int shownRow, int slot) const {
    const Rect area = remapArea();
    const float rowHeight = (area.y1 - area.y0) / static_cast<float>(kRemapVisibleRows + 1);
    const float width = area.x1 - area.x0;
    const float slotWidth = width * (1.0f - kRemapLabelShare) / static_cast<float>(kRemapSlots);
    const float x0 = area.x0 + width * kRemapLabelShare + slotWidth * static_cast<float>(slot);
    const float y0 = area.y0 + rowHeight * static_cast<float>(shownRow + 1);
    return {x0 + slotWidth * 0.04f, y0 + rowHeight * 0.08f, x0 + slotWidth * 0.96f, y0 + rowHeight * 0.92f};
}

bool Menu::remapCellAt(float x, float y, int* row, int* slot) const {
    const int shown = std::min(kRemapVisibleRows, mRemapRowCount - mRemapTop);
    for (int r = 0; r < shown; r++) {
        for (int s = 0; s < kRemapSlots; s++) {
            if (remapCellRect(r, s).contains(x, y)) {
                *row = mRemapTop + r;
                *slot = s;
                return true;
            }
        }
    }
    return false;
}

void Menu::leaveControls() {
    play(Sound::Cancel);
    mRemapMode = RemapMode::Browse;
    mRemapMessage[0] = '\0';
    mPhase = Phase::List;
    mFocus = kListControls;
    mRepeatDir = 0;
}

void Menu::activateRemapCell() {
    if (mRemapRowCount == 0) {
        return;
    }
    play(Sound::Select);
    mResetAllArmed = false;
    mRemapOnButtons = false;
    mRemapMode = RemapMode::Capturing;
    mCaptureFrames = 0.0f;
    mBackend->beginCapture();
    remapMessage("Press a key, mouse button or controller button for %s.  Escape: cancel", mRemapRows[mRemapRow].label);
}

void Menu::finishPlace(RemapOutcome outcome) {
    const char* label = mRemapRows[mRemapRow].label;
    switch (outcome) {
    case RemapOutcome::Done:
        play(Sound::Select);
        remapMessage("%s: %s", label, mCapturedName);
        break;
    case RemapOutcome::Unchanged:
        play(Sound::Cancel);
        remapMessage("%s already has %s.", label, mCapturedName);
        break;
    case RemapOutcome::HomeUnreachable:
        play(Sound::Cancel);
        remapMessage("Not changed: this menu (" PETARI_ICON_HOME ") must keep a keyboard or mouse input.");
        break;
    case RemapOutcome::Conflict:
        break;
    }
    refreshRemapRows();
    char notice[160] = {};
    mBackend->notice(notice, sizeof(notice));
    if (notice[0] != '\0') {
        remapMessage("%s", notice);
    }
}

void Menu::activateRemapButton(int index) {
    if (mRemapMode == RemapMode::Conflict) {
        if (index == 2) {
            play(Sound::Cancel);
            remapMessage("Not changed.");
        } else {
            finishPlace(mBackend->place(mRemapRow, mRemapSlot, index == 0 ? RemapChoice::Swap : RemapChoice::KeepBoth,
                                        mConflictWith, sizeof(mConflictWith)));
        }
        mRemapMode = RemapMode::Browse;
        mRemapOnButtons = false;
        mRemapButton = 0;
        return;
    }
    if (index != 2) {
        mResetAllArmed = false;
    }
    if (mRemapRowCount == 0 && index != 3) {
        return;
    }
    const char* label = mRemapRowCount > 0 ? mRemapRows[mRemapRow].label : "";
    RemapOutcome outcome = RemapOutcome::Unchanged;
    switch (index) {
    case 0:
        outcome = mBackend->clearSlot(mRemapRow, mRemapSlot);
        remapMessage(outcome == RemapOutcome::Done ? "%s: slot cleared." : "%s: that slot is already empty.", label);
        break;
    case 1:
        outcome = mBackend->resetRow(mRemapRow);
        remapMessage(outcome == RemapOutcome::Done ? "%s: back to the default." : "%s already has the default.", label);
        break;
    case 2:
        if (!mResetAllArmed) {
            play(Sound::Select);
            mResetAllArmed = true;
            remapMessage("Press " PETARI_ICON_A " again to put every control back to the default.");
            return;
        }
        mResetAllArmed = false;
        outcome = mBackend->resetAll();
        remapMessage(outcome == RemapOutcome::Done ? "Every control is back to the default." : "Every control already has the default.");
        break;
    default:
        leaveControls();
        return;
    }
    if (outcome == RemapOutcome::HomeUnreachable) {
        remapMessage("Not changed: this menu (" PETARI_ICON_HOME ") must keep a keyboard or mouse input.");
    }
    play(outcome == RemapOutcome::Done ? Sound::Select : Sound::Cancel);
    refreshRemapRows();
    char notice[160] = {};
    mBackend->notice(notice, sizeof(notice));
    if (notice[0] != '\0') {
        remapMessage("%s", notice);
    }
}

void Menu::updateRemap(const FrameInput& input, bool pointerMoved) {
    if (mRemapMode == RemapMode::Capturing) {
        // Everything pressed now goes to the capture, not to the menu.
        const CaptureStatus status = mBackend->pollCapture(mCapturedName, sizeof(mCapturedName));
        mCapturedName[sizeof(mCapturedName) - 1] = '\0';
        if (status == CaptureStatus::Waiting) {
            mCaptureFrames += mConfig.frameDelta;
            if (mCaptureFrames >= kCaptureTimeoutFrames) {
                mBackend->cancelCapture();
                mRemapMode = RemapMode::Browse;
                play(Sound::Cancel);
                remapMessage("Nothing was pressed; not changed.");
            }
            return;
        }
        mRemapMode = RemapMode::Browse;
        if (status == CaptureStatus::Cancelled) {
            play(Sound::Cancel);
            remapMessage("Not changed.");
            return;
        }
        const RemapOutcome outcome =
            mBackend->place(mRemapRow, mRemapSlot, RemapChoice::Check, mConflictWith, sizeof(mConflictWith));
        mConflictWith[sizeof(mConflictWith) - 1] = '\0';
        if (outcome == RemapOutcome::Conflict) {
            play(Sound::Focus);
            mRemapMode = RemapMode::Conflict;
            mRemapOnButtons = true;
            mRemapButton = 2;  // Cancel first: nothing changes by accident
            remapMessage("%s is already %s. Swap them, keep both, or cancel?", mCapturedName, mConflictWith);
            return;
        }
        finishPlace(outcome);
        return;
    }

    if (input.trigger & Button::Home) {
        play(Sound::ReturnApp);
        close();
        mThumbDragging = false;
        return;
    }
    Rect upArrow, downArrow, track, thumb;
    scrollbarRects(&upArrow, &downArrow, &track, &thumb);
    // Dragging the thumb: the list follows the pointer until the button is released.
    if (mThumbDragging) {
        if ((input.hold & Button::B) == 0 || mRemapMode != RemapMode::Browse) {
            mThumbDragging = false;
        } else if (input.pointerValid) {
            const float travel = (track.y1 - track.y0) - (thumb.y1 - thumb.y0);
            const int range = std::max(0, mRemapRowCount - kRemapVisibleRows);
            const float at = travel > 0.0f ? std::clamp((input.pointerY - mThumbGrab - track.y0) / travel, 0.0f, 1.0f) : 0.0f;
            scrollRemapBy(static_cast<int>(std::lround(at * static_cast<float>(range))) - mRemapTop, false);
        }
        return;
    }
    // Wheel and trackpad, Page Up/Down, and the controller's LB/LT (Nunchuk C/Z) page.
    if (mRemapMode == RemapMode::Browse) {
        int pages = input.scrollPages;
        pages += (input.trigger & Button::Z) ? 1 : 0;
        pages -= (input.trigger & Button::C) ? 1 : 0;
        if (input.scrollRows != 0) {
            scrollRemapBy(input.scrollRows, false);
        }
        if (pages != 0) {
            scrollRemapBy(pages * kRemapVisibleRows, true);
        }
    }
    int hoveredRow = -1, hoveredSlot = 0;
    const bool overCell = input.pointerValid && mRemapMode == RemapMode::Browse &&
                          remapCellAt(input.pointerX, input.pointerY, &hoveredRow, &hoveredSlot);
    const int hoveredButton = input.pointerValid ? itemAt(input.pointerX, input.pointerY) : -1;
    if (pointerMoved && overCell && (hoveredRow != mRemapRow || hoveredSlot != mRemapSlot || mRemapOnButtons)) {
        mRemapRow = hoveredRow;
        mRemapSlot = hoveredSlot;
        mRemapOnButtons = false;
        play(Sound::Focus);
    } else if (pointerMoved && hoveredButton >= 0 && (!mRemapOnButtons || hoveredButton != mRemapButton)) {
        mRemapOnButtons = true;
        mRemapButton = hoveredButton;
        play(Sound::Focus);
    }

    if (input.trigger & Button::Plus) {
        if (mRemapMode == RemapMode::Conflict) {
            activateRemapButton(2);
        } else {
            leaveControls();
        }
        return;
    }
    const bool a = (input.trigger & Button::A) != 0;
    const bool b = (input.trigger & Button::B) != 0;
    // The scrollbar, with the pointer's button (B, the left mouse button).
    if (b && input.pointerValid && mRemapMode == RemapMode::Browse) {
        const float x = input.pointerX, y = input.pointerY;
        if (upArrow.contains(x, y) || downArrow.contains(x, y)) {
            scrollRemapBy(upArrow.contains(x, y) ? -1 : 1, false);
            return;
        }
        if (thumb.contains(x, y)) {
            mThumbDragging = true;
            mThumbGrab = y - thumb.y0;
            return;
        }
        if (track.contains(x, y)) {
            scrollRemapBy(y < thumb.y0 ? -kRemapVisibleRows : kRemapVisibleRows, false);
            return;
        }
    }
    if (a || (b && (overCell || hoveredButton >= 0))) {
        if (b && overCell) {
            mRemapRow = hoveredRow;
            mRemapSlot = hoveredSlot;
            mRemapOnButtons = false;
        } else if (b) {
            mRemapOnButtons = true;
            mRemapButton = hoveredButton;
        }
        if (mRemapOnButtons) {
            activateRemapButton(mRemapButton);
        } else {
            activateRemapCell();
        }
        return;
    }
    if (b) {
        // A click on the panel's empty space does nothing; elsewhere B is back.
        if (input.pointerValid && remapPanel().contains(input.pointerX, input.pointerY)) {
            return;
        }
        if (mRemapMode == RemapMode::Conflict) {
            activateRemapButton(2);
        } else {
            leaveControls();
        }
        return;
    }

    const std::uint32_t side = input.trigger & (Button::Left | Button::Right);
    const int sideStep = side == Button::Left ? -1 : side == Button::Right ? 1 : 0;
    const int steps = verticalStep(input);
    if (mRemapMode == RemapMode::Conflict) {
        // Only the three choices.
        const int move = sideStep != 0 ? sideStep : (steps > 0 ? 1 : steps < 0 ? -1 : 0);
        if (move != 0) {
            mRemapButton = std::clamp(mRemapButton + move, 0, itemCount() - 1);
            play(Sound::Focus);
        }
        return;
    }
    if (sideStep != 0) {
        if (mRemapOnButtons) {
            mRemapButton = std::clamp(mRemapButton + sideStep, 0, itemCount() - 1);
        } else {
            mRemapSlot = std::clamp(mRemapSlot + sideStep, 0, kRemapSlots - 1);
        }
        mResetAllArmed = mResetAllArmed && mRemapOnButtons && mRemapButton == 2;
        play(Sound::Focus);
        return;
    }
    for (int i = 0; i < std::abs(steps); i++) {
        if (steps > 0) {
            if (mRemapOnButtons) break;
            if (mRemapRow + 1 < mRemapRowCount) {
                mRemapRow++;
            } else {
                mRemapOnButtons = true;  // past the last row: the buttons
                mRemapButton = std::min(mRemapSlot, itemCount() - 1);
            }
        } else if (mRemapOnButtons) {
            mRemapOnButtons = false;
        } else if (mRemapRow > 0) {
            mRemapRow--;
        }
        play(Sound::Focus);
    }
    if (steps != 0) {
        mResetAllArmed = false;
        scrollToRow();
    }
}

// --- Game instance and published view ---

namespace {
std::mutex gViewMutex;
View gView;
}  // namespace

Menu& instance() {
    static Menu menu;
    return menu;
}

View publishedView() {
    std::lock_guard<std::mutex> lock(gViewMutex);
    return gView;
}

void publish(const View& view) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    gView = view;
}

}  // namespace PetariNative::HomeMenu
