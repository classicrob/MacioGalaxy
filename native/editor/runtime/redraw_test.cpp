// Live check that a re-create in Edit draws the object once (native/tools/editor_sync_smoke.py):
// with PETARI_EDITOR=1 and PETARI_EDITOR_REDRAW_TEST=<placement name>, once the stage smoke's
// checks are done it enters Edit through the UI (the scene frozen, as for a person; the LevelEditor
// mod on), drags the first bound row of that name (previews) and drops it (an op, as the gizmo
// does), and compares the actors connected to draw before and after. A move must leave as many
// drawn actors as before, and the new ones must be the row's re-created actors (an NPC's LOD
// models, partners and the like included). Logs "[editor-redraw] PASS" or "[editor-redraw] FAIL: <reason>".
#include "runtime_core.hpp"

#include <petari/host_allocation.hpp>

#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <petari/editor_settings.hpp>
#include <petari/smoke_phase.hpp>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    struct State {
        int frame = -1, waited = 0;
        bool done = false;
        RowId row;
        LiveActor* original = nullptr;
        std::set< LiveActor* > drawnBefore;
    };
    State gRedraw;

    void finish(const std::string& result) {
        std::fprintf(stderr, "[editor-redraw] %s\n", result.c_str());
        gRedraw.done = true;
    }

    std::set< LiveActor* > drawnActors() {
        std::set< LiveActor* > out;
        for (s32 i = 0; i < EditorScene::liveActorCount(); i++) {
            LiveActor* pActor = EditorScene::actorAt(i);
            if (pActor != nullptr && EditorScene::isConnectedToDraw(pActor)) out.insert(pActor);
        }
        return out;
    }

    std::string describe(LiveActor* pActor) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s (model %s, dead %d, hidden %d) at %.0f %.0f %.0f", pActor->mName ? pActor->mName : "?",
                      pActor->mModelManager ? MR::getModelResName(pActor) : "none", MR::isDead(pActor), MR::isHiddenModel(pActor),
                      pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z);
        return buffer;
    }
}  // namespace

void redrawTestTick() {
    PetariNative::HostAllocationScope host;  // test bookkeeping (the actors it checks are only read)
    static const char* const sClass = [] {
        const char* p = std::getenv("PETARI_EDITOR_REDRAW_TEST");
        return p != nullptr && p[0] != '\0' ? p : nullptr;
    }();
    State& s = gRedraw;
    if (sClass == nullptr || s.done) return;
    if (s.frame < 0) {
        if (!PetariNative::SmokePhase::stageChecksDone.load()) return;
        s.frame = 0;
        PetariNative::EditorSettings::toggle();  // a Tab tap: the UI enters Edit and tells Runtime
        return;
    }
    if (s.frame == 0 && mode() != Mode::Editing) {
        if (++s.waited > 600) finish("FAIL: Edit mode never started (is the LevelEditor mod on?)");
        return;
    }
    const int f = ++s.frame;
    const ModelView* m = model();
    if (f == 5) {
        if (m == nullptr) return finish("FAIL: no model: " + status().lastError);
        forEachBoundRow([&](RowId id, LiveActor* pActor) {
            const char* pName = m->objectName(id);
            if (!s.row.valid() && pName != nullptr && std::string(pName) == sClass) {
                s.row = id;
                s.original = pActor;
            }
        });
        if (!s.row.valid()) return finish(std::string("FAIL: no bound ") + sClass + " row");
        s.drawnBefore = drawnActors();
    }
    if (f >= 5 && f < 9) {
        // The drag: previews of the live actor, 50 units a frame.
        RowTransform t = m->transform(s.row);
        t.pos.x += 50.0f * (f - 4);
        Command preview{Command::Kind::Preview};
        preview.rows = {s.row};
        preview.previews = {t};
        enqueue(std::move(preview));
    }
    if (f == 9) {
        Command c{Command::Kind::Apply};
        c.op.label = "Move";
        c.op.sets.push_back(SetCell{s.row, columnHash("pos_x"), Cell{CellType::Float, 0, m->transform(s.row).pos.x + 200.0f, {}}});
        enqueue(std::move(c));
    }
    if (f == 15) {
        LiveActor* pNew = actorForRow(s.row);
        if (pNew == nullptr || pNew == s.original) return finish("FAIL: the row was not re-created");
        const std::set< LiveActor* > after = drawnActors();
        std::vector< LiveActor* > added, removed;
        for (LiveActor* a : after)
            if (!s.drawnBefore.count(a)) added.push_back(a);
        for (LiveActor* a : s.drawnBefore)
            if (!after.count(a)) removed.push_back(a);
        std::fprintf(stderr, "[editor-redraw] %s: %zu drawn before, %zu after (+%zu -%zu)\n", sClass, s.drawnBefore.size(), after.size(),
                     added.size(), removed.size());
        for (LiveActor* a : added) std::fprintf(stderr, "[editor-redraw]   + %s%s\n", describe(a).c_str(), a == pNew ? " [the row]" : "");
        for (LiveActor* a : removed) std::fprintf(stderr, "[editor-redraw]   - %s%s\n", describe(a).c_str(), a == s.original ? " [the row]" : "");
        for (LiveActor* a : after) {
            const char* pModel = a->mModelManager ? MR::getModelResName(a) : "";
            if (pModel != nullptr && std::string(pModel).find(sClass) != std::string::npos)
                std::fprintf(stderr, "[editor-redraw]   drawn: %s\n", describe(a).c_str());
        }
        if (EditorScene::isConnectedToDraw(s.original)) return finish("FAIL: the replaced actor is still drawn");
        if (after.size() != s.drawnBefore.size()) return finish("FAIL: a move changed how many actors are drawn");
        return finish("PASS");
    }
}

}  // namespace PetariEditor::Runtime
