#pragma once
// Fullscreen mode and the Mac cursor (native/CONTROLS.md).
//
// Cmd+Return switches between the window and fullscreen. Fullscreen covers the
// whole display without a separate Space, so the menu bar and the Dock stay
// hidden even at the screen's edges. In fullscreen the Mac cursor is hidden
// while the game has focus (the game draws its own Star Pointer); it shows
// again while the F1 menu is open, so menu items can still be clicked.
//
// display.txt in the user directory: Fullscreen=on|off, default off. Every
// switch is saved, so the game starts the way it was left.

#include <filesystem>
#include <string>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

namespace PetariNative::App::Display {

bool load(const std::filesystem::path& file, std::string* error);
bool fullscreen();  // the saved choice: AuroraConfig::startFullscreen

// Before the window exists: fullscreen without a separate Space.
void configureHints();
void attach(SDL_Window* window);
// Every SDL event on the event thread: Cmd+Return toggles; the window's own
// fullscreen changes are saved.
void handleEvent(const SDL_Event& event);
// Once per frame on the event thread, after the events: shows or hides the cursor.
void update();

}  // namespace PetariNative::App::Display
