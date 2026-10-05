#pragma once
// Native replacement for the Wii HOME Button Menu.
//
// On the Wii, HomeButtonLayout drives the SDK's HOME Button Menu, which the
// game loads as PowerPC machine code (HomeButtonMenuWrapperRSO.rso). That
// module cannot run natively, so the native branch of HomeButtonMenuWrapper.cpp
// binds the wrapper's seven HBM entry points to this menu instead. The menu
// keeps the contract HomeButtonLayout relies on:
//
// - HBMCreate once; HBMInit each time the layout becomes active; HBMCalc every
//   active frame with the game's own KPAD data (MR::getHBMKPadData).
// - HBMGetSelectBtnNum stays HBM_SELECT_NULL while the menu runs, which keeps
//   the scene paused. It reports a selection only after the closing
//   animation: HBM_SELECT_HOMEBTN (resume) after a fade out, HBM_SELECT_BTN1
//   (quit; the game calls OSReturnToMenu, which exits the process natively) and
//   HBM_SELECT_BTN2 (application reset to the title) after a fade to black,
//   because the layout then starts its reset with the screen already black.
// - HBMStartBlackOut (a console reset while the menu is open) fades to black
//   and reports HBM_SELECT_BTN2; the layout's _25 flag only matters for that
//   result. While the menu is closed it has no effect.
//
// The menu offers Mac actions (Resume, Controls, Mods, Restart from Title, Quit) and
// does not present Wii system features. Controls is a remapper over the input
// layer's bindings when the bridge sets its ControlsBackend (always in the game),
// else the read-only list of setControls. Rendering is a Dear ImGui overlay drawn in the
// host frame (drawImGuiOverlay); the state machine itself has no rendering or
// SDK dependencies.
//
// Controls come from the game's KPAD data, so the approved bindings apply
// unchanged (native/CONTROLS.md, native/input):
// - Home (F1): resume.
// - Plus (Escape): back; from the list it resumes.
// - A (Space, right mouse): activate the focused item, or the item under the
//   pointer.
// - B alone (left mouse): activate the item under the pointer; elsewhere, back.
// - Up/Down (arrow keys, W/S via the Nunchuk stick): move the focus, with
//   hold repeat.
// - Pointer (mouse): moving onto an item focuses it.

#include <algorithm>
#include <cstdint>
#include <string>

struct ImFont;

namespace PetariNative::HomeMenu {

// The game's picture-font icons (GalaxyFont::Picture), as UTF-8 that menu text
// can embed: picture character c is U+E000 + (c - 0x20). Drawn in their own
// colors with the game's fonts, as "(A)" and so on without them.
#define PETARI_ICON_A "\xEE\x80\x90"        // U+E010: the A button
#define PETARI_ICON_B "\xEE\x80\x91"        // U+E011: B
#define PETARI_ICON_C "\xEE\x80\x92"        // U+E012: Nunchuk C
#define PETARI_ICON_SHAKE "\xEE\x80\x93"    // U+E013: the Wii Remote (shake)
#define PETARI_ICON_ONE "\xEE\x80\x95"      // U+E015: 1
#define PETARI_ICON_TWO "\xEE\x80\x96"      // U+E016: 2
#define PETARI_ICON_STICK "\xEE\x80\x9F"    // U+E01F: the Control Stick
#define PETARI_ICON_PAD "\xEE\x80\xA3"      // U+E023: the + Control Pad
#define PETARI_ICON_HOME "\xEE\x80\xA6"     // U+E026: HOME
#define PETARI_ICON_MINUS "\xEE\x80\xA7"    // U+E027: -
#define PETARI_ICON_PLUS "\xEE\x80\xA8"     // U+E028: +
#define PETARI_ICON_Z "\xEE\x80\xA9"        // U+E029: Nunchuk Z

// Values equal HBMSelectBtnNum.
enum class Selection : int { None = -1, Resume = 0, Quit = 1, Restart = 2 };

enum class Phase : std::uint8_t {
    Closed,    // before the first open
    Opening,   // fade in; input ignored
    List,      // Resume / Controls / Mods / My Progress / Restart from Title / Quit
    Confirm,   // confirming Restart or Quit
    Controls,  // the controls page, with Back
    Mods,      // on/off toggles for the native mods, Level Select, Back
    Levels,    // Level Select: galaxy, mission, Go, Back (native/LEVEL_SELECT.md)
    Camera,    // Odyssey camera mod options (docs/dev/ODYSSEY_CAMERA.md)
    Progress,  // My Progress: the personal clear record, one galaxy at a time (native/PROGRESS.md)
    ModFolder, // disc-file mods found in the mods folder: toggles, paging, Back (native/MODS.md)
    Closing,   // fade out before reporting Resume
    BlackOut,  // fade to black before reporting Restart or Quit
    Finished,  // selection() reports the result
};

// Controller buttons as the menu reads them (bits of FrameInput).
namespace Button {
constexpr std::uint32_t Up = 1u << 0;
constexpr std::uint32_t Down = 1u << 1;
constexpr std::uint32_t A = 1u << 2;
constexpr std::uint32_t B = 1u << 3;
constexpr std::uint32_t Plus = 1u << 4;
constexpr std::uint32_t Home = 1u << 5;
constexpr std::uint32_t Left = 1u << 6;   // change a value (Level Select)
constexpr std::uint32_t Right = 1u << 7;
constexpr std::uint32_t C = 1u << 8;      // Nunchuk C (controller LB): page up in the controls list
constexpr std::uint32_t Z = 1u << 9;      // Nunchuk Z (controller LT): page down
}  // namespace Button

// One frame of controller input, all controllers combined.
struct FrameInput {
    std::uint32_t hold = 0;
    std::uint32_t trigger = 0;
    bool pointerValid = false;
    // KPAD pointer position: -1..1 across the game image, y down.
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    // Host scrolling for lists (Input::takeMenuScroll): rows (wheel, trackpad) and
    // pages (Page Up/Down), + toward later rows.
    int scrollRows = 0;
    int scrollPages = 0;
};

// HBM sound events, as HBMSoundCallback receives them (see revolution/hbm).
namespace Sound {
constexpr int EventBeginExitAnim = 2;  // HBMSEV_BEGIN_EXIT_ANIM
constexpr int EventBeginBlackOut = 3;  // HBMSEV_BEGIN_BLACKOUT
constexpr int EventEndMenu = 4;        // HBMSEV_END_MENU
constexpr int EventPlaySound = 5;      // HBMSEV_PLAY_SOUND; num is one of:
constexpr int HomeButton = 0;          // HBMSE_HOME_BUTTON
constexpr int ReturnApp = 1;           // HBMSE_RETURN_APP
constexpr int GotoMenu = 2;            // HBMSE_GOTO_MENU
constexpr int ResetApp = 3;            // HBMSE_RESET_APP
constexpr int Focus = 4;               // HBMSE_FOCUS
constexpr int Select = 5;              // HBMSE_SELECT
constexpr int Cancel = 6;              // HBMSE_CANCEL
}  // namespace Sound

using SoundCallback = int (*)(int event, int num);

struct Config {
    SoundCallback sound = nullptr;
    // Animation time per HBMCalc, in 60 Hz frames (HBMDataInfo::frameDelta).
    float frameDelta = 1.0f;
    // Horizontal correction for a 16:9 image (HBMDataInfo::adjust.x), applied
    // while the widescreen flag (HBMSetAdjustFlag) is set so the panel keeps
    // its shape.
    float widescreenAdjustX = 832.0f / 608.0f;
};

// Animation lengths in 60 Hz frames, and focus repeat timing.
constexpr float kFadeFrames = 10.0f;
constexpr float kBlackOutFrames = 30.0f;
constexpr float kRepeatDelayFrames = 24.0f;
constexpr float kRepeatIntervalFrames = 6.0f;

// Rectangles in KPAD pointer space (-1..1 across the game image, y down).
struct Rect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    bool contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

constexpr int kMaxMods = 12;         // Mods::Mod::Count must stay <= this (the Mods page has one toggle each)
// The Mods page lists every mod toggle, then Odyssey camera, Level Select, Mod folder and Back.
constexpr int kModsPageExtraItems = 4;
// Sized from the worst page (the Mods page); other pages use at most 6 items. View::items
// must never be indexed past this: a write past it lands on View::lineCount.
constexpr int kMaxItems = kMaxMods + kModsPageExtraItems;
static_assert(kMaxItems >= kMaxMods + kModsPageExtraItems, "the Mods page must fit in View::items");
constexpr int kMaxControls = 20;
constexpr int kMaxProgressLines = 8;   // the My Progress page: a header and up to 7 missions; <= kMaxControls
static_assert(kMaxProgressLines <= kMaxControls, "the progress lines are shown through View::lines");
constexpr int kMaxFolderMods = 64;   // detected disc-file mods (native/MODS.md)
constexpr int kFolderPageSize = 3;   // toggles per page of the Mod folder page

// The Odyssey camera mod's options (Mods -> Odyssey camera).
struct CameraOptions {
    bool on = false;
    int speed = 3;  // 1..5
    bool invertX = false;
    bool invertY = false;
    bool photo = false;  // photo mode (P), independent of the orbit camera
};

// One toggle of the Mods page: what the mod does, and whether it is on.
struct ModsEntry {
    char label[48] = {};
    bool on = false;
    int needs = -1;  // index of a mod this one needs: on without it, the line says so
};

// One disc-file mod of the Mod folder page: its label (title, file count) and state.
struct FolderModEntry {
    char label[64] = {};
    bool on = false;
};

// One line of the Controls page: what the player does, the keyboard and
// mouse inputs, and the game controller's (empty: none). An empty action
// marks a column header.
struct ControlsEntry {
    char action[40] = {};
    char inputs[64] = {};
    char pad[40] = {};
};

// --- The Controls page's remapping (native/CONTROLS.md "Changing controls") ---
// With a ControlsBackend set, the Controls page lists every action with four
// input slots (two keyboard/mouse, two controller) and edits them: A on a slot
// captures the next pressed input, conflicts offer Swap / Keep both / Cancel,
// and Clear, Reset action, Reset all and Back sit below the table. Without one
// it shows the read-only summary of setControls.

constexpr int kRemapSlots = 4;          // keyboard/mouse 0 and 1, controller 2 and 3
constexpr int kRemapVisibleRows = 11;   // rows shown at once; the table scrolls
constexpr int kMaxRemapRows = 48;
constexpr float kCaptureTimeoutFrames = 600.0f;  // 10 s without a press cancels a capture

struct RemapRow {
    char label[48] = {};                   // may start with an icon (PETARI_ICON_*)
    char slots[kRemapSlots][32] = {};      // input names, "" for an empty slot
    bool conflict[kRemapSlots] = {};       // the input also drives another action (beyond the defaults' own sharing)
    bool changed = false;                  // differs from the defaults
};

enum class CaptureStatus : std::uint8_t { Waiting, Captured, Cancelled };
enum class RemapChoice : std::uint8_t { Check, Swap, KeepBoth };
enum class RemapOutcome : std::uint8_t {
    Done,             // applied (and saved)
    Unchanged,
    Conflict,         // RemapChoice::Check only: another action has the input; nothing changed
    HomeUnreachable,  // refused: this menu would lose its last keyboard or mouse input
};

// What the page edits; the bridge implements it with the input layer
// (petari/input_remap.hpp), tests with a fake. Called on the menu's thread.
class ControlsBackend {
public:
    virtual ~ControlsBackend() = default;
    virtual int rowCount() = 0;
    virtual void row(int index, RemapRow* out) = 0;
    virtual void beginCapture() = 0;
    virtual void cancelCapture() = 0;
    // Waiting until a press; Captured keeps the input for place() and names it in *name.
    virtual CaptureStatus pollCapture(char* name, int size) = 0;
    // Puts the captured input in a slot. Conflict: *other names the action that has it.
    virtual RemapOutcome place(int row, int slot, RemapChoice choice, char* other, int size) = 0;
    virtual RemapOutcome clearSlot(int row, int slot) = 0;
    virtual RemapOutcome resetRow(int row) = 0;
    virtual RemapOutcome resetAll() = 0;
    // A standing message for the page ("" for none): an invalid controls.txt, a failed save.
    virtual void notice(char* out, int size) = 0;
};

struct RemapView {
    bool active = false;
    int rowCount = 0;   // rows shown
    int firstRow = 0;   // index of rows[0] among totalRows
    int totalRows = 0;
    RemapRow rows[kRemapVisibleRows];
    int focusRow = -1;  // in rows[], -1 when the focus is on the buttons
    int focusSlot = 0;
    int selectedRow = -1;   // the row the buttons act on, in rows[] (-1: scrolled away)
    int selectedSlot = 0;
    bool capturing = false;  // focusRow/focusSlot is waiting for a press
    Rect area;               // the table, header row included
    Rect cells[kRemapVisibleRows][kRemapSlots];  // the slots of the shown rows (pointer targets)
    // The scrollbar right of the table: arrows at both ends, the track between
    // them, and the thumb (its size the shown fraction of the rows).
    Rect upArrow, downArrow, track, thumb;
    bool dragging = false;   // the thumb is held
};

struct ViewItem {
    const char* label = "";
    Rect rect;
    bool focused = false;
};

// What the overlay draws.
struct View {
    bool visible = false;
    Phase phase = Phase::Closed;
    float dimOpacity = 0.0f;    // darkening of the paused game behind the panel
    float panelOpacity = 0.0f;  // panel, text, and items
    float blackOpacity = 0.0f;  // full-image black, over everything
    Rect panel;
    // The list and the confirmations: no panel; `panel` is a full-width band across
    // the top (title and message), with a matching band along the bottom.
    bool bands = false;
    const char* title = "";
    const char* message = "";  // may be empty
    char footer[192] = {};     // the buttons that work on this page, with icons; drawn along the image bottom
    float titleY = 0.0f;       // text centers, KPAD space
    float messageY = 0.0f;
    int itemCount = 0;
    ViewItem items[kMaxItems];
    // The Controls page: lines drawn as columns in linesArea, one row each
    // (three when any line has controller text).
    int lineCount = 0;
    ControlsEntry lines[kMaxControls];
    Rect linesArea;
    RemapView remap;  // the Controls page with a ControlsBackend
};

class Menu {
public:
    void create(const Config& config);  // HBMCreate
    void setWidescreen(bool widescreen);  // HBMSetAdjustFlag
    void open();                          // HBMInit
    void update(const FrameInput& input);  // HBMCalc
    void startBlackOut();                 // HBMStartBlackOut
    // The Controls page contents (at most kMaxControls lines; longer text is
    // cut). Kept across opens.
    void setControls(const ControlsEntry* entries, int count);
    // Makes the Controls page a remapper (null: the read-only summary). Not owned.
    void setControlsBackend(ControlsBackend* backend);
    // The Mods page toggles (at most kMaxMods). Kept across opens.
    void setMods(const ModsEntry* entries, int count);
    // Toggles the player activated since the last call: bit i is entry i,
    // whose new state is in mods()[i]. The caller applies and saves them.
    std::uint32_t takeModToggles();
    // The Mod folder page (native/MODS.md): the disc-file mods found, most
    // recent scan. Calling this (even with none) adds a "Mod folder..." entry
    // to the Mods page. Changes apply when the game next starts.
    void setFolderMods(const FolderModEntry* entries, int count);
    // Folder mods toggled since the last call: bit i is entry i (new state in
    // folderMods()[i]); the caller saves them to mods.txt.
    std::uint64_t takeFolderToggles();
    const FolderModEntry* folderMods() const { return mFolderMods; }
    int folderModCount() const { return mFolderCount; }
    const ModsEntry* mods() const { return mMods; }
    // Level Select's Go, reported once after the menu has closed: the
    // internal stage name and mission. The caller starts the warp.
    bool takeLevelRequest(const char** stage, int* mission);
    void setEditorEnabled(bool enabled);
    bool takeNewGalaxyRequest();
    int levelGalaxy() const { return mLevelGalaxy; }
    // The Odyssey camera page's options; changes are reported once (true) and
    // copied to *options. The caller applies and saves them.
    void setCamera(const CameraOptions& options);
    bool takeCameraChange(CameraOptions* options);
    const CameraOptions& camera() const { return mCamera; }
    int levelMission() const { return mLevelMission; }
    // Keyboard names for the footer's buttons (A, Plus, Home, the + Control
    // Pad's left/right); empty names show the icon alone. Kept across opens.
    void setFooterKeys(const char* confirm, const char* back, const char* close, const char* change);
    Selection selection() const { return mPhase == Phase::Finished ? mResult : Selection::None; }  // HBMGetSelectBtnNum

    Phase phase() const { return mPhase; }
    int focus() const { return mFocus; }
    // Pending action while in Confirm (Restart or Quit).
    Selection confirming() const { return mConfirming; }
    View view() const;

private:
    int itemCount() const;      // the page's item count, capped at kMaxItems
    int pageItemCount() const;  // uncapped
    Rect itemRect(int index) const;
    float adjustX() const { return mWidescreen ? mConfig.widescreenAdjustX : 1.0f; }
    void play(int num) const;
    void event(int event) const;
    void setFocus(int focus, bool withSound);
    void moveFocus(int delta);
    void activate(int index);
    void back();
    void close();
    void blackOut(Selection result);
    void finish();
    int itemAt(float x, float y) const;

    Config mConfig;
    bool mCreated = false;
    bool mWidescreen = false;
    Phase mPhase = Phase::Closed;
    Selection mResult = Selection::None;
    Selection mConfirming = Selection::None;
    int mFocus = 0;
    float mTime = 0.0f;          // frames into the current animation
    float mBlackStart = 0.0f;    // black opacity when the black-out began
    float mPanelStart = 0.0f;    // panel opacity when the closing began
    std::uint32_t mRepeatDir = 0;  // Button::Up or Button::Down being repeated
    float mRepeatTime = 0.0f;
    bool mPointerWasValid = false;
    float mPointerX = 0.0f;
    float mPointerY = 0.0f;
    int mControlCount = 0;
    ControlsEntry mControls[kMaxControls];
    int mModCount = 0;
    ModsEntry mMods[kMaxMods];
    char mModLabels[kMaxMods][64] = {};
    std::uint32_t mModToggles = 0;
    bool mFolderPage = false;
    int mFolderCount = 0;
    int mFolderPageIndex = 0;
    FolderModEntry mFolderMods[kMaxFolderMods];
    char mFolderLabels[kFolderPageSize][96] = {};
    char mFolderMessage[128] = {};
    std::uint64_t mFolderToggles = 0;
    int mProgressGalaxy = 0;  // index into LaunchStage::galaxies
    bool mProgressResetArmed = false;
    char mProgressLabels[4][96] = {};
    char mProgressMessage[160] = {};
    ControlsEntry mProgressLines[kMaxProgressLines];
    int mProgressLineCount = 0;
    void refreshProgress();
    void changeProgressGalaxy(int delta);
    int folderPages() const { return (mFolderCount + kFolderPageSize - 1) / kFolderPageSize; }
    int folderShown() const { return std::clamp(mFolderCount - mFolderPageIndex * kFolderPageSize, 0, kFolderPageSize); }
    int modsBackIndex() const { return mModCount + (mFolderPage ? 3 : 2); }
    void refreshFolderLabels();
    int mLevelGalaxy = 0;   // index into LaunchStage::galaxies
    int mLevelMission = 1;
    bool mLevelPending = false;
    bool mEditorEnabled = false;
    bool mNewGalaxyPending = false;
    char mLevelLabels[2][80] = {};
    char mLevelMessage[96] = {};
    void refreshLevelLabels();
    char mFooterKeys[4][32] = {};  // confirm, back, close, change
    // The remapper (Controls page with a backend).
    enum class RemapMode : std::uint8_t { Browse, Capturing, Conflict };
    ControlsBackend* mBackend = nullptr;
    RemapMode mRemapMode = RemapMode::Browse;
    RemapRow mRemapRows[kMaxRemapRows];
    int mRemapRowCount = 0;
    int mRemapRow = 0;        // the focused (or last focused) row
    int mRemapSlot = 0;
    bool mRemapOnButtons = false;
    int mRemapButton = 0;     // Clear, Reset action, Reset all, Back; or Swap, Keep both, Cancel
    int mRemapTop = 0;        // first row shown
    float mCaptureFrames = 0.0f;
    bool mResetAllArmed = false;
    bool mThumbDragging = false;
    float mThumbGrab = 0.0f;  // pointer y minus the thumb's top when it was grabbed
    void scrollRemapBy(int rows, bool moveFocus);
    void scrollbarRects(Rect* up, Rect* down, Rect* track, Rect* thumb) const;
    Rect remapPanel() const;
    char mCapturedName[32] = {};
    char mConflictWith[48] = {};
    char mRemapMessage[192] = {};
    void refreshRemapRows();
    void updateRemap(const FrameInput& input, bool pointerMoved);
    void activateRemapCell();
    void activateRemapButton(int index);
    void finishPlace(RemapOutcome outcome);
    void remapMessage(const char* format, ...);
    void scrollToRow();
    bool remapCellAt(float x, float y, int* row, int* slot) const;
    Rect remapCellRect(int shownRow, int slot) const;
    Rect remapArea() const;
    int verticalStep(const FrameInput& input);
    void leaveControls();
    CameraOptions mCamera;
    bool mCameraChanged = false;
    char mCameraLabels[5][48] = {};
    void refreshCameraLabels();
    void refreshModLabels();
    void changeCameraValue(int item, int delta);
    void changeLevelValue(int item, int delta);
};

// --- The game's menu instance (HomeButtonMenuWrapper.cpp's native branch) ---

Menu& instance();
// Snapshot published by HBMDraw, hidden once a selection is reported. Any thread.
View publishedView();
void publish(const View& view);

// Loads the game's fonts from the disc's LayoutData/Font.arc (galaxy_font.hpp)
// into the ImGui font atlas, so the menus use the game's lettering and button
// icons. Call once on the thread that owns the ImGui context, after it exists
// and outside a frame (the atlas is rebuilt before the next frame). False, with
// a message, when the file is missing or unreadable: the menus then keep
// ImGui's built-in font. Target petari_home_menu_imgui.
bool installGalaxyFonts(const char* fontArchive, std::string* error);
bool galaxyFontsInstalled();
// The game's fonts in the ImGui atlas (null without them), for other ImGui
// surfaces that want the Galaxy lettering (the level editor's titles and tabs).
ImFont* galaxyTextFont();   // MessageFont26, with the picture icons at U+E000..
ImFont* galaxyTitleFont();  // MenuFont64

// Draws publishedView() with Dear ImGui (target petari_home_menu_imgui). Call
// on the thread that owns the ImGui context, between aurora_begin_frame() and
// aurora_end_frame(). The rectangle is the game image in ImGui display
// coordinates. Draws nothing while the menu is hidden. The overlay window
// takes no input, so ImGui never captures the game's keyboard or mouse.
void drawImGuiOverlay(float imageX, float imageY, float imageWidth, float imageHeight);
// The hold-to-skip ring (Mods::Mod::SkipCutscenes). The overlay asks this once per frame while the
// menu is hidden: 0 or less draws nothing, 0..1 fills a ring in the top right corner (1.5 s of
// holding = 1), 1 while a skip runs shows it full. The app points it at petari_skip_progress
// (native/include/petari/skip.hpp) by default when the input layer is in the build, else null (no
// ring); pass null to disable it. With the input layer the ring names the key bound to A
// (Space by default) rather than showing the A icon. Any thread, set once.
using HoldProgressFn = float (*)();
void setHoldProgressSource(HoldProgressFn source);
// Draws the ring for `progress` (used by drawImGuiOverlay; exposed for the render tool and tests).
// `key` is the name of the key being held ("Space"), shown in the ring; null or "" shows the
// controller's A icon. Same thread and frame rules as drawImGuiOverlay.
void drawHoldRing(float progress, float imageX, float imageY, float imageWidth, float imageHeight, const char* key = nullptr);
// The Point-to-move steering cue (Mods::Mod::PointToMove): while the left button steers Mario, a
// faint arrow from Mario toward the pointer shows where he is headed. The overlay asks this once per
// frame while the menu is hidden. It returns true only while the cue is active and fills Mario's
// and the pointer's positions in KPAD space (-1..1 across the game image, y down). The app points it
// at petari_point_move_cue (native/include/petari/point_move.hpp) by default when the input layer is in
// the build; null disables the cue.
using SteerCueFn = bool (*)(float* marioX, float* marioY, float* pointerX, float* pointerY);
void setSteerCueSource(SteerCueFn source);
// Draws the cue (used by drawImGuiOverlay; exposed for the render tool and tests).
void drawSteerCue(float marioX, float marioY, float pointerX, float pointerY, float imageX, float imageY, float imageWidth,
                  float imageHeight);
// The title screen's keyboard hint (ASCII), in a bar across the top of the
// image, above the logo. drawImGuiOverlay draws it while the input layer
// reports the title's "press A and B" prompt and the menu is hidden, when
// the input layer is in the build. Same thread and frame rules.
void drawTitleHint(const char* text, float imageX, float imageY, float imageWidth, float imageHeight);
// The hint's bar in KPAD space (-1..1 across the image, y down).
Rect titleHintRect();

}  // namespace PetariNative::HomeMenu
