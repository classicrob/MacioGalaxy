// Live check of play-test progress isolation (native/tools/editor_sync_smoke.py): with
// PETARI_EDITOR=1 and PETARI_EDITOR_SCRATCH_TEST=1, once the stage smoke's checks are done it opens
// an edit session (ensureModel: progress isolated) with 12 lives, earns 100 Star Bits and 3 lives,
// then turns the editor off (the session ends): both must be back where the session found them
// (lives too, which the save data's own read resets to 4), and isolation released.
// Logs "[editor-scratch] PASS" or "[editor-scratch] FAIL: <reason>".
#include "runtime_core.hpp"

#include <petari/editor/runtime.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/host_allocation.hpp>
#include <petari/smoke_phase.hpp>

#include "Game/System/GameDataFunction.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    struct State {
        int frame = -1;
        bool done = false;
        s32 before = 0;
        s32 livesBefore = 0;
        bool supplyBefore = false;
    };
    State gScratch;

    void finish(const std::string& result) {
        std::fprintf(stderr, "[editor-scratch] %s\n", result.c_str());
        gScratch.done = true;
    }
}  // namespace

// Before the editor-off check in tick: it must run on the frame the editor is turned off too.
void scratchTestTick() {
    static const bool sOn = [] {
        const char* p = std::getenv("PETARI_EDITOR_SCRATCH_TEST");
        return p != nullptr && p[0] == '1';
    }();
    State& s = gScratch;
    if (!sOn || s.done || !PetariNative::SmokePhase::stageChecksDone.load()) return;
    const int f = ++s.frame;
    if (f == 0) {
        GameDataFunction::addPlayerLeft(12 - GameDataFunction::getPlayerLeft());  // a non-default count
        s.before = GameDataFunction::getStarPieceNum();
        s.livesBefore = GameDataFunction::getPlayerLeft();
        s.supplyBefore = GameDataFunction::isPlayerLeftSupply();
        if (!ensureModel() || !progressIsolated()) return finish("FAIL: no session, or progress not isolated");
        GameDataFunction::addStarPiece(100);
        GameDataFunction::addPlayerLeft(3);
        std::fprintf(stderr, "[editor-scratch] in the session: Star Bits %d -> %d\n", s.before, GameDataFunction::getStarPieceNum());
        if (GameDataFunction::getStarPieceNum() == s.before) return finish("FAIL: the Star Bits didn't change in the session");
        Hooks::setEnabled(false);  // the editor turned off: the session ends on the next tick
        return;
    }
    if (f == 3) {
        const s32 after = GameDataFunction::getStarPieceNum();
        std::fprintf(stderr, "[editor-scratch] after the session: Star Bits %d (found %d), lives %d (found %d), isolated %d\n", after, s.before,
                     GameDataFunction::getPlayerLeft(), s.livesBefore, progressIsolated());
        if (after != s.before) return finish("FAIL: the play-test's Star Bits reached the save file's progress");
        if (GameDataFunction::getPlayerLeft() != s.livesBefore) return finish("FAIL: the lives weren't restored");
        if (GameDataFunction::isPlayerLeftSupply() != s.supplyBefore) return finish("FAIL: the 1-Up supply changed (the extra-life letter)");
        if (progressIsolated()) return finish("FAIL: isolation still on after the session");
        return finish("PASS");
    }
}

}  // namespace PetariEditor::Runtime
