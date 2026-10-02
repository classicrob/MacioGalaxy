#pragma once
// Smoke-run phase flags that game-side test drivers wait on. Header-only, so neither side needs
// the other linked in (the app tests build smoke_stage.cpp without the game).
#include <atomic>

namespace PetariNative::SmokePhase {
// The stage smoke (native/app/smoke_stage.cpp) has finished its checks and is idling.
inline std::atomic< bool > stageChecksDone{false};
}  // namespace PetariNative::SmokePhase
