// Live check of Runtime::ensureModel (native/tools/editor_sync_smoke.py): with PETARI_EDITOR=1 and
// PETARI_EDITOR_ENSURE_TEST=1, once the stage smoke's checks are done it loads the model in Play
// (Edit never entered), moves Good Egg's Octoomba by an op and checks the running scene re-created
// it 300 units up, still outside Edit. Logs "[editor-ensure] PASS" or "[editor-ensure] FAIL: <reason>".
#include "runtime_core.hpp"

#include <petari/editor/runtime.hpp>
#include <petari/host_allocation.hpp>

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <petari/smoke_phase.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    struct State {
        int frame = -1;
        bool done = false;
        RowId takobo;
        LiveActor* original = nullptr;
        TVec3f originalPos;
    };
    State gEnsure;

    void finish(const std::string& result) {
        std::fprintf(stderr, "[editor-ensure] %s\n", result.c_str());
        gEnsure.done = true;
    }
}  // namespace

void ensureTestTick() {
    PetariNative::HostAllocationScope host;  // test bookkeeping (the actors it checks are only read)
    static const bool sOn = [] {
        const char* p = std::getenv("PETARI_EDITOR_ENSURE_TEST");
        return p != nullptr && p[0] == '1';
    }();
    State& s = gEnsure;
    if (!sOn || s.done || !PetariNative::SmokePhase::stageChecksDone.load()) return;
    const int f = ++s.frame;
    if (f == 0) {
        if (!ensureModel()) return finish("FAIL: ensureModel: " + status().lastError);
        const ModelView* m = model();
        if (m == nullptr || mode() != Mode::Off) return finish("FAIL: no model, or Edit was entered");
        forEachBoundRow([&](RowId id, LiveActor* pActor) {
            const char* pName = m->objectName(id);
            if (!s.takobo.valid() && pName != nullptr && std::string(pName) == "Takobo") {
                s.takobo = id;
                s.original = pActor;
            }
        });
        if (!s.takobo.valid()) return finish("FAIL: no bound Takobo");
        s.originalPos = s.original->mPosition;
        Command move{Command::Kind::Apply};
        move.op.label = "Move Octoomba";
        move.op.sets.push_back(SetCell{s.takobo, columnHash("pos_y"), Cell{CellType::Float, 0, m->transform(s.takobo).pos.y + 300.0f, {}}});
        enqueue(std::move(move));
        return;
    }
    if (f == 10) {
        LiveActor* pNow = actorForRow(s.takobo);
        const float d = pNow ? std::sqrt((pNow->mPosition.x - s.originalPos.x) * (pNow->mPosition.x - s.originalPos.x) +
                                         (pNow->mPosition.y - s.originalPos.y) * (pNow->mPosition.y - s.originalPos.y) +
                                         (pNow->mPosition.z - s.originalPos.z) * (pNow->mPosition.z - s.originalPos.z))
                             : 0.0f;
        std::fprintf(stderr, "[editor-ensure] in Play: re-created %d, moved %.0f, mode %d, history %d\n", pNow != s.original, d,
                     static_cast< int >(mode()), model()->historySize());
        if (pNow == nullptr || pNow == s.original || !MR::isDead(s.original)) return finish("FAIL: the op didn't re-create the actor");
        if (d < 200.0f || d > 450.0f) return finish("FAIL: the new actor isn't 300 units up");
        if (mode() != Mode::Off) return finish("FAIL: Edit was entered");
        return finish("PASS");
    }
}

}  // namespace PetariEditor::Runtime
