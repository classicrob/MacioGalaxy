// Live check of SceneSync without the UI (native/tools/editor_sync_smoke.py): with PETARI_EDITOR=1
// and PETARI_EDITOR_SYNC_TEST=1, once the stage smoke's checks are done it plays the UI's part
// through edit_api.hpp on Good Egg: enter Edit; move a placed Octoomba by an op (its actor must be
// re-created at the new place); undo (re-created back); preview a Coin and cancel (moved, then
// back); an edit SceneSync can't apply live (pending); duplicate the Octoomba (an added row with
// its own actor) and delete the copy (its actor retired). Logs "[editor-sync] PASS" or
// "[editor-sync] FAIL: <reason>".
#include "runtime_core.hpp"

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
    float distance(const TVec3f& a, const TVec3f& b) {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    RowId findBound(const char* pZone, const char* pTable, const char* pName, bool needActor = true) {
        const ModelView* m = model();
        for (int z = 0; z < m->zoneCount(); z++) {
            if (std::string(m->zoneName(z)) != pZone) continue;
            for (int t = 0; t < m->tableCount(z); t++) {
                if (std::string(m->tablePath(z, t)) != pTable) continue;
                RowId found;
                m->forEachRow(z, t, [&](RowId id) {
                    const char* pObj = m->objectName(id);
                    if (!found.valid() && pObj != nullptr && std::string(pObj) == pName && (!needActor || actorForRow(id) != nullptr)) found = id;
                });
                return found;
            }
        }
        return {};
    }

    struct State {
        RowId copy;
        LiveActor* copyActor = nullptr;
        int frame = -1;
        bool done = false;
        RowId takobo, coin, start;
        LiveActor* original = nullptr;
        LiveActor* moved = nullptr;
        TVec3f originalPos, coinPos;
    };
    State gState;

    void finish(const char* pResult) {
        std::fprintf(stderr, "[editor-sync] %s\n", pResult);
        gState.done = true;
        notifyEditLeft();
    }

    Command apply(const char* pLabel, RowId row, const char* pColumn, Cell value) {
        Command c{Command::Kind::Apply};
        c.op.label = pLabel;
        c.op.sets.push_back(SetCell{row, columnHash(pColumn), value});
        return c;
    }
}  // namespace

void syncTestTick() {
    PetariNative::HostAllocationScope host;  // test bookkeeping (the actors it checks are only read)
    static const bool sOn = [] {
        const char* p = std::getenv("PETARI_EDITOR_SYNC_TEST");
        return p != nullptr && p[0] == '1';
    }();
    State& s = gState;
    if (!sOn || s.done) return;
    if (s.frame < 0) {
        if (!PetariNative::SmokePhase::stageChecksDone.load()) return;
        s.frame = 0;
        notifyEditEntered();
        return;
    }
    const int f = ++s.frame;
    const ModelView* m = model();
    if (f == 5) {
        if (m == nullptr) return finish(("FAIL: no model: " + status().lastError).c_str());
        s.takobo = findBound("SoramameZone", "jmp/placement/layera/objinfo", "Takobo");
        s.coin = findBound("SoramameZone", "jmp/placement/layera/objinfo", "Coin");
        s.start = findBound("EggStarGalaxy", "jmp/start/common/startinfo", "Mario", false);
        if (!s.takobo.valid() || !s.coin.valid()) return finish("FAIL: no bound Takobo or Coin row in Soramame layer A");
        s.original = actorForRow(s.takobo);
        s.originalPos = s.original->mPosition;
        const float y = m->transform(s.takobo).pos.y;
        enqueue(apply("Move Octoomba", s.takobo, "pos_y", Cell{CellType::Float, 0, y + 300.0f, {}}));
    }
    if (f == 10) {
        s.moved = actorForRow(s.takobo);
        const float d = s.moved ? distance(s.moved->mPosition, s.originalPos) : 0;
        std::fprintf(stderr, "[editor-sync] after the op: new actor %d, original dead %d, moved %.0f\n", s.moved != s.original,
                     MR::isDead(s.original), d);
        if (s.moved == nullptr || s.moved == s.original || !MR::isDead(s.original) || d < 200.0f || d > 400.0f)
            return finish("FAIL: the edited row's actor was not re-created 300 units away");
        if (model()->historyCursor() != 1 || !status().unsaved) return finish("FAIL: the op is not in the history");
        enqueue(Command{Command::Kind::Undo});
    }
    if (f == 15) {
        LiveActor* back = actorForRow(s.takobo);
        const float d = back ? distance(back->mPosition, s.originalPos) : 1e9f;
        std::fprintf(stderr, "[editor-sync] after undo: %.0f from the original place\n", d);
        if (back == nullptr || back == s.moved || !MR::isDead(s.moved) || d > 100.0f)
            return finish("FAIL: undo did not re-create the actor at its place");
        LiveActor* coin = actorForRow(s.coin);
        s.coinPos = coin->mPosition;
        RowTransform t = m->transform(s.coin);
        t.pos.x += 500.0f;
        Command preview{Command::Kind::Preview};
        preview.rows = {s.coin};
        preview.previews = {t};
        enqueue(std::move(preview));
    }
    if (f == 17) {
        const float d = distance(actorForRow(s.coin)->mPosition, s.coinPos);
        std::fprintf(stderr, "[editor-sync] preview moved the Coin %.0f\n", d);
        if (d < 400.0f || d > 600.0f) return finish("FAIL: the preview did not move the Coin's actor");
        Command end{Command::Kind::EndPreview};
        end.rows = {s.coin};
        enqueue(std::move(end));
    }
    if (f == 19) {
        const float d = distance(actorForRow(s.coin)->mPosition, s.coinPos);
        if (d > 5.0f) return finish("FAIL: cancelling the preview did not put the Coin back");
        if (status().capacities.empty() || status().capacities[0].used <= 0 || status().heapFree == 0 || status().pendingCount != 0)
            return finish("FAIL: status (capacities, heap, pending)");
        if (s.start.valid()) {
            const float y = m->transform(s.start).pos.y;
            enqueue(apply("Move start", s.start, "pos_y", Cell{CellType::Float, 0, y + 10.0f, {}}));
        }
    }
    if (f == 22) {
        if (s.start.valid() && (status().pendingCount != 1 || !status().dirty))
            return finish("FAIL: an edit to a start point should wait for a reload");
        std::fprintf(stderr, "[editor-sync] objects %d/%d, gravity %d/%d, archives %d/%d, scene heap %u KB free of %u KB\n",
                     status().capacities[0].used, status().capacities[0].limit, status().capacities[1].used, status().capacities[1].limit,
                     status().capacities[2].used, status().capacities[2].limit, status().heapFree / 1024, status().heapTotal / 1024);
        // Duplicate the Octoomba 500 units away: an added row with its own live actor.
        int zone = 0, table = 0;
        m->locate(s.takobo, &zone, &table);
        s.copy = newRowId();
        Command dup{Command::Kind::Apply};
        dup.op.label = "Duplicate Octoomba";
        AddRow add;
        add.id = s.copy;
        add.zone = zone;
        add.table = m->tablePath(zone, table);
        add.copyOf = s.takobo;
        add.cells.push_back({columnHash("pos_x"), Cell{CellType::Float, 0, m->transform(s.takobo).pos.x + 500.0f, {}}});
        dup.op.adds.push_back(add);
        enqueue(std::move(dup));
    }
    if (f == 26) {
        s.copyActor = actorForRow(s.copy);
        LiveActor* original = actorForRow(s.takobo);
        const float d = s.copyActor && original ? distance(s.copyActor->mPosition, original->mPosition) : 0;
        std::fprintf(stderr, "[editor-sync] duplicate: state %d, actor %d, %.0f from the original\n", static_cast< int >(model()->state(s.copy)),
                     s.copyActor != nullptr, d);
        if (s.copyActor == nullptr || MR::isDead(s.copyActor) || model()->state(s.copy) != RowState::Added || d < 350.0f)
            return finish("FAIL: the duplicate has no live actor of its own");
        Command del{Command::Kind::Apply};
        del.op.label = "Delete Octoomba";
        del.op.deletes.push_back(s.copy);
        enqueue(std::move(del));
    }
    if (f == 30) {
        if (actorForRow(s.copy) != nullptr || !MR::isDead(s.copyActor) || model()->state(s.copy) != RowState::Deleted)
            return finish("FAIL: deleting the duplicate left its actor");
        std::fprintf(stderr, "[editor-sync] new problems after the edits: %zu\n", checks().size());
        for (size_t i = 0; i < checks().size() && i < 12; i++) {
            std::fprintf(stderr, "[editor-sync]   %s %s\n", checks()[i].severity == Severity::Error ? "error" : "warning",
                         checks()[i].message.c_str());
        }
        if (!checks().empty()) return finish("FAIL: Checks report problems the edits didn't make");
        // A switch id out of range: a new error, with a fix.
        enqueue(apply("Bad switch", s.takobo, "SW_B", Cell{CellType::Int, 500, 0, {}}));
    }
    if (f == 33) {
        const Check* pFound = nullptr;
        for (const Check& c : checks()) {
            if (c.key.rfind("switch-range:", 0) == 0 && !c.rows.empty() && c.rows.front() == s.takobo) pFound = &c;
        }
        if (pFound == nullptr || pFound->fix.sets.empty()) return finish("FAIL: no out-of-range switch check");
        std::fprintf(stderr, "[editor-sync] check: %s (fix: %s)\n", pFound->message.c_str(), pFound->fixLabel.c_str());
        finish("PASS");
    }
}

}  // namespace PetariEditor::Runtime
