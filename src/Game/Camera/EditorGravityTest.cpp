#ifdef PETARI_NATIVE
#include "Game/Camera/EditorGravity.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Gravity.hpp"
#include "Game/Util.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/host_allocation.hpp>
#include <petari/screenshot.hpp>
#include <petari/smoke_phase.hpp>
#include <string>

namespace EditorGravity {
    void testTick(bool playable) {
        using namespace PetariEditor;
        static const char* test = std::getenv("PETARI_EDITOR_GRAVITY_TEST");
        static int step = -1, frame = 0, count = 0;
        static RowId row;
        static LiveActor* original = nullptr;
        static Vec3 before;
        if (!test || step == 999)
            return;
        PetariNative::HostAllocationScope host;
        auto fail = [&](const char* reason) {
            std::fprintf(stderr, "[editor-gravity] FAIL: %s\n", reason);
            step = 999;
        };
        if (step < 0) {
            if (std::string(test) != "1" && !std::filesystem::exists(test))
                return;
            if (!playable)
                return;
            PetariNative::EditorSettings::requestEnter();
            step = 0;
            return;
        }
        if (mode() != Mode::Editing) {
            if (++frame > 1200)
                fail("Edit did not start");
            return;
        }
        ++frame;
        auto* m = model();
        if (!m)
            return;
        if (step == 0) {
            if (Gravity::view().fields.empty())
                return;
            for (const auto& f : Gravity::view().fields)
                if (f.row.valid() && f.shape == Gravity::Shape::Point) {
                    row = f.row;
                    before = f.center;
                    break;
                }
            if (!row.valid())
                return fail("No row-backed point field");
            original = actorForRow(row);
            count = EditorScene::gravityCount();
            Gravity::setVisible(true);
            PetariNative::EditorPick::selectRow(row.v, PetariNative::EditorPick::Click::Replace);
            PetariNative::EditorSettings::frame();
            PetariNative::EditorPick::setHoverWanted(true);
            PetariNative::EditorSettings::cursor(0, 0);
            step = 1;
            frame = 0;
            return;
        }
        if (step == 1 && frame == 15) {
            Command c{Command::Kind::Preview};
            c.rows = {row};
            auto t = m->transform(row);
            t.pos.x += 250;
            c.previews = {t};
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 20) {
            bool moved = false;
            for (const auto& f : Gravity::view().fields)
                if (f.row == row)
                    moved = std::fabs(f.center.x - before.x) > 50 || std::fabs(f.center.z - before.z) > 50;
            if (!moved)
                return fail("Gizmo preview did not move the actual field");
            Command c{Command::Kind::EndPreview};
            c.rows = {row};
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 25) {
            for (const auto& f : Gravity::view().fields)
                if (f.row == row && std::fabs(f.center.x - before.x) > 1)
                    return fail("Preview cancellation did not restore field");
            Command c{Command::Kind::Apply};
            c.op = Gravity::numericOp(*m, {row}, "Priority", 100, true);
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 30) {
            if (actorForRow(row) != original || EditorScene::gravityCount() != count)
                return fail("Priority edit spent a gravity slot");
            auto* g = static_cast< GlobalGravityObj* >(original)->getGravity();
            if (g->mPriority != 100)
                return fail("Priority did not reach engine");
            if (MR::getSceneObj< PlanetGravityManager >(SceneObj_PlanetGravityManager)->nativeGravityAt(0) != g)
                return fail("Priority did not re-sort manager");
            Command c{Command::Kind::Apply};
            c.op = Gravity::numericOp(*m, {row}, "Range", 20);
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 35) {
            auto* g = static_cast< GlobalGravityObj* >(original)->getGravity();
            if (g->mRange != 20)
                return fail("Range did not reach engine");
            Command c{Command::Kind::Apply};
            c.op = Gravity::numericOp(*m, {row}, "Range", -1);
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 40) {
            auto* g = static_cast< GlobalGravityObj* >(original)->getGravity();
            if (g->mRange != -1)
                return fail("Range -1 did not restore infinity");
            Command c{Command::Kind::Undo};
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 45) {
            auto* g = static_cast< GlobalGravityObj* >(original)->getGravity();
            if (g->mRange != 20)
                return fail("Undo did not restore range");
            enqueue(Command{Command::Kind::Redo});
        }
        if (step == 1 && frame >= 50 && frame < 150) {
            Command c{Command::Kind::Apply};
            c.op = Gravity::numericOp(*m, {row}, "Priority", static_cast< float >(frame), true);
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 155) {
            if (actorForRow(row) != original || EditorScene::gravityCount() != count)
                return fail("Repeated in-place edits consumed slots");
            Command c{Command::Kind::Apply};
            c.op = Gravity::shapeOp(*m, row, Gravity::Shape::Disk);
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 165) {
            if (actorForRow(row) == original || !actorForRow(row))
                return fail("Shape edit did not replace field");
            if (static_cast< GlobalGravityObj* >(original)->getGravity()->mAppeared)
                return fail("Old field remains active after shape edit");
            if (EditorScene::gravityCount() != count + 1)
                return fail("Shape edit slot accounting wrong");
            std::fprintf(stderr, "[editor-gravity] shape, range, priority, preview, undo/redo and 100 updates verified; slots %d -> %d\n", count,
                         EditorScene::gravityCount());
            if (const char* shot = std::getenv("PETARI_EDITOR_GRAVITY_SHOT"))
                PetariNative::Screenshot::request(shot);
        }
        if (step == 1 && frame == 195) {
            std::fprintf(stderr, "[editor-gravity] PASS\n");
            step = 999;
        }
    }
}  // namespace EditorGravity
#endif
