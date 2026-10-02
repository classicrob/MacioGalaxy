// Live check of the reload from the model and Play (native/tools/editor_reload_smoke.py): with
// PETARI_EDITOR=1 and PETARI_EDITOR_RELOAD_TEST=1, once the stage smoke's checks are done it plays
// the UI's part on Good Egg: enter Edit; move a placed Octoomba (live) and a start point (pending);
// Reload: the new scene must place the Octoomba where the edit put it (from the edited archive),
// with nothing pending, back in Edit; then Play from a marker: Mario must stand there. Edit is
// entered and re-entered through the UI (EditorSettings, the LevelEditor mod on). Logs
// "[editor-reload] PASS" or "[editor-reload] FAIL: <reason>".
#include "runtime_core.hpp"

#include <petari/host_allocation.hpp>

#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <petari/editor_settings.hpp>
#include <petari/smoke_phase.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    float dist(const TVec3f& a, const TVec3f& b) {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    RowId findRow(const char* pZone, const char* pTable, const char* pName, bool needActor) {
        const ModelView* m = model();
        for (int z = 0; z < m->zoneCount(); z++) {
            if (std::string(m->zoneName(z)) != pZone) continue;
            for (int t = 0; t < m->tableCount(z); t++) {
                if (std::string(m->tablePath(z, t)) != pTable) continue;
                RowId found;
                m->forEachRow(z, t, [&](RowId id) {
                    const char* pObj = m->objectName(id);
                    if (!found.valid() && pObj && std::string(pObj) == pName && (!needActor || actorForRow(id))) found = id;
                });
                return found;
            }
        }
        return {};
    }

    struct State {
        LiveActor* originalActor = nullptr;
        int frame = -1, phase = 0, phaseFrame = 0;
        bool done = false;
        RowId takobo;
        TVec3f original, marker;
    };
    State gReload;

    void finish(const char* pResult) {
        std::fprintf(stderr, "[editor-reload] %s\n", pResult);
        gReload.done = true;
    }

    Command setFloat(const char* pLabel, RowId row, const char* pColumn, float value) {
        Command c{Command::Kind::Apply};
        c.op.label = pLabel;
        c.op.sets.push_back(SetCell{row, columnHash(pColumn), Cell{CellType::Float, 0, value, {}}});
        return c;
    }
}  // namespace

void reloadTestTick() {
    PetariNative::HostAllocationScope host;  // test bookkeeping (the actors it checks are only read)
    static const bool sOn = [] {
        const char* p = std::getenv("PETARI_EDITOR_RELOAD_TEST");
        return p != nullptr && p[0] == '1';
    }();
    State& s = gReload;
    if (!sOn || s.done) return;
    if (s.frame < 0) {
        if (!PetariNative::SmokePhase::stageChecksDone.load()) return;
        s.frame = 0;
        PetariNative::EditorSettings::toggle();  // a Tab tap: the UI enters Edit and tells Runtime
        return;
    }
    s.frame++;
    s.phaseFrame++;
    const ModelView* m = model();
    switch (s.phase) {
    case 0:
        if (mode() != Mode::Editing) {
            if (s.phaseFrame > 600) return finish("FAIL: Edit mode never started (is the LevelEditor mod on?)");
            return;
        }
        if (s.phaseFrame < 5) return;
        if (m == nullptr) return finish(("FAIL: no model: " + status().lastError).c_str());
        s.takobo = findRow("SoramameZone", "jmp/placement/layera/objinfo", "Takobo", true);
        if (!s.takobo.valid()) return finish("FAIL: no bound Takobo");
        s.original = actorForRow(s.takobo)->mPosition;
        s.originalActor = actorForRow(s.takobo);
        enqueue(setFloat("Move Octoomba", s.takobo, "pos_y", m->transform(s.takobo).pos.y + 300.0f));
        if (RowId start = findRow("EggStarGalaxy", "jmp/start/common/startinfo", "Mario", false); start.valid())
            enqueue(setFloat("Move start", start, "pos_y", m->transform(start).pos.y));  // same value: a pending no-op edit
        s.phase = 10;  // first: the live re-create in the frozen scene, then Reload
        s.phaseFrame = 0;
        return;
    case 10:
        if (s.phaseFrame < 3) return;
        // Edit mode is frozen (no movement step): the replaced actor must still leave the draw lists.
        if (EditorScene::isConnectedToDraw(s.originalActor)) return finish("FAIL: the replaced Octoomba is still drawn in Edit mode");
        std::fprintf(stderr, "[editor-reload] frozen re-create: the old actor left the draw lists\n");
        enqueue(Command{Command::Kind::Reload});
        s.phase = 1;
        s.phaseFrame = 0;
        return;
    case 1:
        if (s.phaseFrame % 120 == 0) {
            std::fprintf(stderr, "[editor-reload] waiting: mode %d, frame %d\n", static_cast< int >(mode()), s.phaseFrame);
        }
        if (mode() == Mode::Reloading) {
            if (s.phaseFrame > 1200) return finish("FAIL: the reload did not finish in 20 s");
            return;
        }
        if (mode() != Mode::Editing) {
            if (s.phaseFrame > 1500) return finish("FAIL: not back in Edit after the reload");
            return;
        }
        {
            LiveActor* a = actorForRow(s.takobo);
            const float d = a ? dist(a->mPosition, s.original) : 0;
            std::fprintf(stderr, "[editor-reload] after the reload: Octoomba %.0f from its disc place, pending %d, history %d\n", d,
                         status().pendingCount, model()->historyCursor());
            if (a == nullptr || d < 200.0f || d > 450.0f) return finish("FAIL: the reloaded scene did not place the edited Octoomba");
            if (status().pendingCount != 0) return finish("FAIL: edits still pending after the reload");
            if (model()->historyCursor() != 2) return finish("FAIL: the undo history did not survive the reload");
        }
        {
            TVec3f mario = *MR::getPlayerPos();
            TVec3f front;
            MR::getPlayerFrontVec(&front);
            s.marker = TVec3f(mario.x + front.x * 400.0f, mario.y + front.y * 400.0f + 50.0f, mario.z + front.z * 400.0f);
            Command marker{Command::Kind::SetPlayMarker};
            marker.markerPos = Vec3{s.marker.x, s.marker.y, s.marker.z};
            enqueue(std::move(marker));
            Command play{Command::Kind::Play};
            play.from = PlayFrom::Marker;
            enqueue(std::move(play));
        }
        s.phase = 2;
        s.phaseFrame = 0;
        return;
    case 2:
        if (s.phaseFrame < 60) return;
        {
            const float d = dist(*MR::getPlayerPos(), s.marker);
            std::fprintf(stderr, "[editor-reload] Play: mode %d, Mario %.0f from the marker\n", static_cast< int >(mode()), d);
            if (mode() != Mode::Playing) return finish("FAIL: Play did not switch to Playing");
            if (d > 150.0f) return finish("FAIL: Mario is not at the play marker");
        }
        return finish("PASS");
    }
}

}  // namespace PetariEditor::Runtime
