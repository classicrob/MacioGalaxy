#pragma once
// Smoke-run phase flags that game-side test drivers wait on. Header-only, so neither side needs
// the other linked in (the app tests build smoke_stage.cpp without the game).
#include <atomic>

namespace PetariNative::SmokePhase {
// The stage smoke (native/app/smoke_stage.cpp) has finished its checks and is idling.
inline std::atomic< bool > stageChecksDone{false};
// A game-side check is done while the stage smoke follows the game (PETARI_STAGE_FOLLOW=1).
inline std::atomic< bool > followDone{false};
// The level editor is rebuilding the scene (native/editor runtime_hooks.hpp setEditorReload): the
// player is briefly gone on purpose.
inline std::atomic< bool > editorReloading{false};
}  // namespace PetariNative::SmokePhase
