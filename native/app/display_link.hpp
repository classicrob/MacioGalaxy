#pragma once
// Display refresh grid for the VI clock lock (petari/present_pacing.hpp).

namespace PetariNative::App::DisplayLink {

// Main thread, after Aurora created the window's Metal view. With
// PETARI_VI_CLOCK=display: starts a CADisplayLink on its own thread that
// publishes the refresh grid. Otherwise nothing.
void start(void* window);

}  // namespace PetariNative::App::DisplayLink
