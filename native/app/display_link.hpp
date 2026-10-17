#pragma once
// Display refresh grid for present pacing (petari/present_pacing.hpp).

namespace PetariNative::App::DisplayLink {

// Main thread, after Aurora created the window's Metal view. With
// PETARI_PRESENT_PACING=displaylink: starts a CADisplayLink on its own thread
// that publishes the refresh grid, and prints a pacing summary at exit.
// Otherwise nothing.
void start(void* window);

}  // namespace PetariNative::App::DisplayLink
