#ifdef PETARI_NATIVE
#include "Game/Camera/EditorGravity.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Gravity.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor/paths.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/host_allocation.hpp>
#include <petari/screenshot.hpp>
#include <petari/smoke_phase.hpp>
#include <string>
#include <vector>

namespace EditorGravity {
    void testTick(bool playable) {
        using namespace PetariEditor;
        static const char* test = std::getenv("PETARI_EDITOR_GRAVITY_TEST");
        static int step = -1, frame = 0, count = 0;
        static RowId row;
        static LiveActor* original = nullptr;
        static Vec3 before;
        static std::vector< bool > active;
        static LiveActor* atCap = nullptr;
        static RowId railRow;
        static Paths::Rail testRail;
        static Vec3 wireStart;
        static LiveActor* wireActor = nullptr;
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
        if (step == 1 && frame == 10)
            PetariNative::EditorSettings::frame();
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
                    moved = std::fabs(f.center.x - before.x) > 50 || std::fabs(f.center.y - before.y) > 50 || std::fabs(f.center.z - before.z) > 50;
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
        const Gravity::Shape shapes[] = {Gravity::Shape::Cube, Gravity::Shape::Cone, Gravity::Shape::Torus,
                                         Gravity::Shape::Parallel, Gravity::Shape::Segment, Gravity::Shape::Wire};
        if (step == 1 && frame >= 170 && frame <= 270 && (frame - 170) % 20 == 0) {
            Command c{Command::Kind::Apply};
            c.op = Gravity::shapeOp(*m, row, shapes[(frame - 170) / 20]);
            if (c.op.sets.empty()) return fail("Shape conversion produced no operation");
            enqueue(std::move(c));
        }
        if (step == 1 && frame >= 180 && frame <= 280 && (frame - 180) % 20 == 0) {
            const int index = (frame - 180) / 20;
            bool found = false;
            for (const auto& f : Gravity::view().fields)
                if (f.row == row && f.shape == shapes[index]) found = true;
            if (!found) return fail("Shape family did not reach live engine snapshot");
            if (EditorScene::gravityCount() != count + 2 + index)
                return fail("Shape family conversion did not consume exactly one slot");
        }
        if (step == 1 && frame == 240) {
            int zone = 0, table = 0;
            m->locate(row, &zone, &table);
            const auto pos = m->transform(row).pos;
            Vec3 end{pos.x + 600, pos.y + 100, pos.z};
            railRow = newRowId();
            Command c{Command::Kind::Apply};
            c.op = Paths::addRail(*m, zone, {{newRowId(), pos, pos, pos}, {newRowId(), end, end, end}}, false, railRow, nullptr);
            if (c.op.adds.empty()) return fail("Could not add wire fixture rail");
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 265) {
            for (const auto& rail : Paths::collect(*m))
                if (rail.row == railRow) testRail = rail;
            if (testRail.points.size() != 2) return fail("Wire fixture rail not visible in model");
            Command c{Command::Kind::Apply};
            c.op = Paths::linkPath(*m, row, testRail, nullptr);
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 285) {
            wireActor = actorForRow(row);
            bool found = false;
            for (const auto& f : Gravity::view().fields)
                if (f.row == row && !f.points.empty()) { wireStart = f.points.front(); found = true; }
            if (!found) return fail("Wire has no engine sample points");
            auto point = testRail.points.front();
            auto pos = point.position;
            pos.x += 200;
            Command c{Command::Kind::Apply};
            c.op = Paths::movePoint(point, pos);
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 300) {
            bool moved = false;
            for (const auto& f : Gravity::view().fields)
                if (f.row == row && !f.points.empty()) {
                    const auto p = f.points.front();
                    moved = std::fabs(p.x - wireStart.x) + std::fabs(p.y - wireStart.y) + std::fabs(p.z - wireStart.z) > 50;
                }
            if (!moved || actorForRow(row) != wireActor || EditorScene::gravityCount() != count + 7)
                return fail("Wire rail edit did not update in place without spending a slot");
            std::fprintf(stderr, "[editor-gravity] all eight shape families and live wire path edits verified\n");
        }
        if (step == 1 && frame == 380) {
            auto* mgr = MR::getSceneObj< PlanetGravityManager >(SceneObj_PlanetGravityManager);
            for (int i = 0; i < mgr->nativeGravityCount(); i++) {
                auto* g = mgr->nativeGravityAt(i);
                active.push_back(g->mActivated);
                g->mActivated = false;
            }
        }
        if (step == 1 && frame == 440) {
            if (Gravity::view().deadGround.empty())
                return fail("Uncovered ground was not marked red");
            const auto& problems = checks();
            if (std::none_of(problems.begin(), problems.end(), [](const auto& c) { return c.key.rfind("gravity-ground:", 0) == 0; }))
                return fail("Uncovered ground did not reach Checks");
            if (const char* shot = std::getenv("PETARI_EDITOR_GRAVITY_SHOT")) {
                std::string dead = std::string(shot) + ".dead.png";
                PetariNative::Screenshot::request(dead.c_str());
            }
            std::fprintf(stderr, "[editor-gravity] uncovered ground: %zu red samples, Checks verified\n", Gravity::view().deadGround.size());
        }
        if (step == 1 && frame == 445) {
            auto* mgr = MR::getSceneObj< PlanetGravityManager >(SceneObj_PlanetGravityManager);
            for (size_t i = 0; i < active.size(); i++)
                mgr->nativeGravityAt(static_cast< int >(i))->mActivated = active[i];
        }
        if (step == 1 && frame == 465) {
            int zone = 0, table = 0;
            m->locate(row, &zone, &table);
            Command c{Command::Kind::Apply};
            c.op.label = "Fill gravity cap for live test";
            for (int n = EditorScene::gravityCount(); n < 128; n++) {
                AddRow add;
                add.id = newRowId();
                add.zone = zone;
                add.table = m->tablePath(zone, table);
                add.copyOf = row;
                c.op.adds.push_back(std::move(add));
            }
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 490) {
            if (EditorScene::gravityCount() != 128)
                return fail("Fixture failed to reach gravity cap");
            const auto& problems = checks();
            if (std::none_of(problems.begin(), problems.end(),
                             [](const auto& c) { return c.key == "gravity-capacity" && c.severity == Severity::Error; }))
                return fail("Full gravity cap did not reach Checks");
            atCap = actorForRow(row);
            Command c{Command::Kind::Apply};
            c.op = Gravity::shapeOp(*m, row, Gravity::Shape::Cube);
            enqueue(std::move(c));
        }
        if (step == 1 && frame == 510) {
            if (actorForRow(row) != atCap || EditorScene::gravityCount() != 128 || !status().dirty)
                return fail("Cap did not retain current field and defer recreation");
            if (!static_cast< GlobalGravityObj* >(atCap)->getGravity()->mAppeared)
                return fail("Cap retired the field before deciding to defer");
            std::fprintf(stderr, "[editor-gravity] full-cap replacement retained current field and awaits Reload\n");
            std::fprintf(stderr, "[editor-gravity] PASS\n");
            step = 999;
        }
    }
}  // namespace EditorGravity
#endif
