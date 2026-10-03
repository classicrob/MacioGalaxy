#pragma once

#ifdef PETARI_NATIVE
#include <revolution.h>
#include <cstdint>

// Level editor spike S2 (class sweep): spawns object creators from synthesized placement rows into a
// running stage and reports what became of each. Driven by PETARI_CLASS_SWEEP_JOBS=<job file>
// (native/tools/class_sweep.py writes it); with the variable unset this does nothing.
namespace ClassSweep {
    // GameScene::update, every frame; actionNerve: the scene is in normal play.
    void update(bool actionNerve);
}  // namespace ClassSweep
#endif
