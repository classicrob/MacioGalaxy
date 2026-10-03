// Live check of terrain brushes through the editor (native/tools/editor_brush_smoke.py): with
// PETARI_EDITOR=1 and PETARI_EDITOR_BRUSH_TEST=1, once the Good Egg stage smoke's checks are done
// it plays the UI's part through edit_api.hpp:
//   1. Enter Edit; add an EditorBrush row (a 3000 x 300 x 3000 checker box, fitted gravity) far
//      from the planets: it must get a live part with collision and a model.
//      The Browser's thumbnail must find its model through the part (EditorTerrain::partModel).
//   2. Put the play marker on it and Play: Mario must land and stand on the brush, with its
//      gravity (straight down). A screenshot is taken (PETARI_EDITOR_BRUSH_SHOTS=<dir>).
//   3. Back in Edit: resize it (scale_x 3 -> 1.5: rebuilt at the new size), change its material
//      (a new model), undo the material, then delete it (its part retired).
// Logs "[editor-brush] PASS" or "[editor-brush] FAIL: <reason>".
#include "runtime_core.hpp"
#include "terrain_sync.hpp"

#include <petari/host_allocation.hpp>
#include <petari/screenshot.hpp>
#include <petari/smoke_phase.hpp>

#include "Game/Editor/EditorScene.hpp"
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace PetariEditor::Runtime {
namespace {
    const float kX = -20000.0f, kY = 0.0f, kZ = 30000.0f;

    struct State {
        int frame = -1;
        bool done = false;
        RowId brush;
        EditorTerrain::Part* part = nullptr;
        LiveActor* firstModel = nullptr;
        int onBrush = 0;
        int playFrame = 0;
        int archives = 0;  // mounted archives before the resize
        TVec3f start;  // Mario before the test (he goes back there before the brush is deleted)
    };
    State gState;

    void finish(const std::string& result) {
        std::fprintf(stderr, "[editor-brush] %s\n", result.c_str());
        gState.done = true;
        PetariNative::SmokePhase::followDone.store(true);
    }

    Cell f(float v) { return Cell{CellType::Float, 0, v, {}}; }
    Cell n(std::int32_t v) { return Cell{CellType::Int, v, 0.0f, {}}; }

    Command apply(const char* pLabel, RowId row, const char* pColumn, Cell value) {
        Command c{Command::Kind::Apply};
        c.op.label = pLabel;
        c.op.sets.push_back(SetCell{row, columnHash(pColumn), value});
        return c;
    }

    EditorTerrain::Part* part(RowId id) {
        return static_cast< EditorTerrain::Part* >(actorForRow(id));
    }

    void shoot(const char* pName) {
        if (const char* pDir = std::getenv("PETARI_EDITOR_BRUSH_SHOTS")) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/%s.png", pDir, pName);
            PetariNative::Screenshot::request(path);
        }
    }
}  // namespace

void brushTestTick() {
    static const bool sOn = [] {
        const char* p = std::getenv("PETARI_EDITOR_BRUSH_TEST");
        return p != nullptr && p[0] == '1';
    }();
    State& s = gState;
    if (!sOn || s.done) {
        return;
    }
    if (s.frame < 0) {
        if (!PetariNative::SmokePhase::stageChecksDone.load()) {
            return;
        }
        s.frame = 0;
        s.start = *MR::getPlayerPos();
        notifyEditEntered();
        return;
    }
    const int frame = ++s.frame;
    const ModelView* m = model();
    PetariNative::HostAllocationScope host;  // test bookkeeping; the game objects are only read
    if (frame == 5) {
        if (m == nullptr) {
            return finish("FAIL: no model: " + status().lastError);
        }
        s.brush = newRowId();
        Command add{Command::Kind::Apply};
        add.op.label = "Add box brush";
        AddRow row;
        row.id = s.brush;
        row.zone = 0;
        row.table = "jmp/placement/common/objinfo";
        row.cells = {{columnHash("name"), Cell{CellType::StringOffset, 0, 0.0f, "EditorBrush"}},
                     {columnHash("pos_x"), f(kX)}, {columnHash("pos_y"), f(kY)}, {columnHash("pos_z"), f(kZ)},
                     {columnHash("dir_x"), f(0)}, {columnHash("dir_y"), f(0)}, {columnHash("dir_z"), f(0)},
                     {columnHash("scale_x"), f(3)}, {columnHash("scale_y"), f(0.3f)}, {columnHash("scale_z"), f(3)},
                     {columnHash("Obj_arg0"), n(0)}, {columnHash("Obj_arg1"), n(0)}, {columnHash("Obj_arg2"), n(0)},
                     {columnHash("Obj_arg3"), n(1)}};
        add.op.adds.push_back(row);
        enqueue(std::move(add));
    }
    if (frame == 10) {
        s.part = part(s.brush);
        std::fprintf(stderr, "[editor-brush] added: part %d, model %d, %zu triangles, state %d\n", s.part != nullptr,
                     s.part != nullptr && s.part->mModel != nullptr, TerrainSync::triangleCount(s.brush), static_cast< int >(m->state(s.brush)));
        if (s.part == nullptr || s.part->mModel == nullptr || s.part->mCollisionParts == nullptr || TerrainSync::triangleCount(s.brush) != 12) {
            return finish("FAIL: the added brush has no live part, model or collision (" + status().lastError + ")");
        }
        if (status().dirty) {
            return finish("FAIL: a brush edit should be live, not pending a reload");
        }
        s.firstModel = s.part->mModel;
        if (EditorTerrain::partModel(s.part) != MR::getJ3DModel(s.part->mModel)) {
            return finish("FAIL: the brush's part does not lead to its model (thumbnails)");
        }
        Command marker{Command::Kind::SetPlayMarker};
        marker.markerPos = {kX + 300.0f, kY + 500.0f, kZ};
        marker.markerUp = {0, 1, 0};
        marker.markerFront = {0, 0, 1};
        enqueue(std::move(marker));
        Command play{Command::Kind::Play};
        play.from = PlayFrom::Marker;
        enqueue(std::move(play));
        s.playFrame = frame;
    }
    if (s.playFrame != 0 && frame > s.playFrame && mode() == Mode::Playing) {
        Triangle* pGround = MR::isOnGroundPlayer() ? MR::getPlayerGroundingPolygon() : nullptr;
        const char* pHost = pGround != nullptr && pGround->mParts != nullptr && pGround->mParts->mHitSensor != nullptr
                                ? pGround->mParts->mHitSensor->mHost->mName : "";
        if (std::strncmp(pHost, "EditorBrush_", 12) == 0) {
            if (++s.onBrush == 60) {
                shoot("brush-play");
                const TVec3f g = *MR::getPlayerGravity();
                std::fprintf(stderr, "[editor-brush] Mario stands on the brush (%s); gravity (%.2f, %.2f, %.2f)\n", pHost, g.x, g.y, g.z);
                if (g.y > -0.95f) {
                    return finish("FAIL: the brush's fitted gravity is not straight down on its top");
                }
            }
        }
        if (frame - s.playFrame > 600 && s.onBrush < 60) {
            const TVec3f* p = MR::getPlayerPos();
            std::fprintf(stderr, "[editor-brush] Mario at (%.0f, %.0f, %.0f), ground %s\n", p->x, p->y, p->z, pHost);
            return finish("FAIL: Mario did not stand on the brush after Play");
        }
        if (s.onBrush == 90) {
            notifyEditEntered();  // back to Edit, as Tab would
            s.playFrame = -frame;
        }
    }
    if (s.playFrame < 0) {
        const int since = frame + s.playFrame;
        if (since == 5) {
            s.archives = status().capacities.size() > 2 ? status().capacities[2].used : -1;
            enqueue(apply("Resize brush", s.brush, "scale_x", f(1.5f)));
        }
        if (since == 10) {
            EditorTerrain::Part* p = part(s.brush);
            const int archives = status().capacities.size() > 2 ? status().capacities[2].used : -1;
            std::fprintf(stderr, "[editor-brush] resized: same part %d, built scale %.2f, new model %d; mounted archives %d -> %d\n", p == s.part,
                         p != nullptr ? p->mBuiltScale.x : 0.0f, p != nullptr && p->mModel != s.firstModel, s.archives, archives);
            if (archives > s.archives) {
                return finish("FAIL: rebuilding the brush's model took another archive slot");
            }
            if (p == nullptr || p != s.part || std::fabs(p->mBuiltScale.x - 1.5f) > 0.01f || p->mModel == s.firstModel) {
                return finish("FAIL: resizing did not rebuild the brush in place");
            }
            shoot("brush-resized");
            enqueue(apply("Brush material", s.brush, "Obj_arg1", n(1)));
        }
        if (since == 15) {
            LiveActor* grassModel = part(s.brush)->mModel;
            enqueue(Command{Command::Kind::Undo});
            if (grassModel == nullptr || MR::isDead(grassModel)) {
                return finish("FAIL: the material change has no live model");
            }
        }
        if (since == 20) {
            const Cell* pArg = m->cell(s.brush, columnHash("Obj_arg1"));
            if (pArg == nullptr || pArg->i != 0) {
                return finish("FAIL: undo did not restore the material");
            }
            EditorReload::teleportPlayer(s.start.x, s.start.y, s.start.z);  // off the brush first
            Command del{Command::Kind::Apply};
            del.op.label = "Delete brush";
            del.op.deletes.push_back(s.brush);
            enqueue(std::move(del));
        }
        if (since == 25) {
            if (actorForRow(s.brush) != nullptr || !MR::isDead(s.part) || TerrainSync::triangleCount(s.brush) != 0) {
                return finish("FAIL: deleting the brush left its part");
            }
            finish("PASS");
        }
    }
}

}  // namespace PetariEditor::Runtime
