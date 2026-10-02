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

// Draws the editor frame: toolbar, dock (Outliner, Inspector) and footer. Call between
// aurora_begin_frame and aurora_end_frame, before the Home menu overlay, every frame.
// windowWidth/Height are the ImGui display size (window points).
void drawFrame(float windowWidth, float windowHeight);

// The central node (the game's viewport) in window points, from the last drawFrame;
// false while hidden or in Focus mode.
bool viewportRect(float* x, float* y, float* width, float* height);

}  // namespace PetariEditor::Ui
