// The level editor's panels (docs/dev/level-editor/UX.md section 4, TECH.md 3.6 and 4.2).
// Dear ImGui only: no SDK or game headers. Main thread, inside the ImGui frame.
#pragma once

namespace PetariEditor::Ui {

// Adds the system UI font (macOS's own, read at run time; nothing is shipped) to the ImGui
// atlas for dense data. Call once after the ImGui context exists and outside a frame, after
// the Galaxy fonts. displayScale is the window's (2 on Retina): the font is rasterised at
// that density and drawn at its point size. False (the editor keeps ImGui's font) if the
// system font cannot be read.
bool installFonts(float displayScale);

// Shows or hides the editor layout. While shown, the game image sits in the dock's central
// node (petari_present_set_content_area) and the panels fill the rest of the window.
void setVisible(bool visible);
bool visible();

// Focus mode (UX section 4: Cmd+.): panels hidden, the game fills the window.
void setFocusMode(bool focus);
bool focusMode();

// The Browser's thumbnails (UX section 7): the host reads a game copy back (petari_present_take_readback:
// 1 ready, 0 pending, -1 rgba too small with width/height set) and makes an ImGui texture of it
// (aurora_imgui_add_texture). Unset: the cards keep their initials tiles.
using ThumbnailReadback = int (*)(const void* copy, unsigned char* rgba, unsigned capacity, unsigned* width, unsigned* height);
using ThumbnailTexture = unsigned long long (*)(unsigned width, unsigned height, const void* rgba);  // an ImTextureID
using ThumbnailTextureUpdate = bool (*)(unsigned long long texture, unsigned width, unsigned height, const void* rgba);
void setThumbnailHost(ThumbnailReadback readback, ThumbnailTexture texture, ThumbnailTextureUpdate update = nullptr);

// Draws the editor frame: toolbar, dock (Outliner, Inspector) and footer. Call between
// aurora_begin_frame and aurora_end_frame, before the Home menu overlay, every frame.
// windowWidth/Height are the ImGui display size (window points).
void drawFrame(float windowWidth, float windowHeight);

// Edit another mission (the toolbar's mission chips): Runtime reloads into it (~2 s, with a toast) and
// keeps the edits and history.
void switchMission(int mission);

// Where the editor keeps its small UI settings (the last mod saved per galaxy, for the Save sheet's
// defaults): <user folder>/editor/ui-settings.txt. Unset: nothing is remembered between sessions.
void setSettingsFile(const char* path);

// A mod zip dropped on the window (UX section 17, Installing): the Install sheet, in or out of Edit.
void openInstallSheet(const char* zipPath);
void submitInstallSheet();  // test drivers: press Install
// Test drivers: the New Galaxy sheet (UX section 14) and its Create.
void openNewGalaxySheetForTest();
void submitNewGalaxySheet();

// While a new galaxy opens after its restart: a "Creating <name>..." banner this frame (UX section 14).
void showOpening(const char* title);

// Test drivers: where a control was drawn last frame, in window points ("tab:Browser", "button:Cancel");
// false if it wasn't drawn.
bool testTargetRect(const char* name, float* x, float* y, float* width, float* height);
// Test drivers: place a class as the Browser's Place does for a card with no example (Obj_arg0 = arg0).
void placeClassForTest(const char* className, int arg0);
// Test drivers: open the Save sheet (as Cmd+Shift+S) and press its Save button.
void openSaveSheet();
void submitSaveSheet();
void openShareSheetForTest();
void submitShareSheet();

// The central node (the game's viewport) in window points, from the last drawFrame;
// false while hidden or in Focus mode.
bool viewportRect(float* x, float* y, float* width, float* height);

}  // namespace PetariEditor::Ui
