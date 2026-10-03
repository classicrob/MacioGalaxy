#include "Game/AreaObj/AreaObj.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "runtime_core.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <petari/editor/areas.hpp>
#include <petari/editor/array.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/host_allocation.hpp>
#include <petari/smoke_phase.hpp>

namespace PetariEditor::Runtime {
    void arrayTestTick() {
        namespace A = Arrays;
        namespace G = Gizmo;
        namespace Pick = PetariNative::EditorPick;
        static const char* enabled = std::getenv("PETARI_EDITOR_ARRAY_TEST");
        static int frame = -1, wait = 0, cursor = 0;
        static bool done = false;
        static RowId source;
        static LiveActor* original = nullptr;
        static TVec3f originalPosition;
        static std::vector< A::Copy > expected;
        static std::vector< RowId > copies;
        static std::vector< LiveActor* > actors;
        static std::uint32_t serial = 0;
        static AreaObjMgr* areaManager = nullptr;
        static int areaCount = 0;
        static AreaObj* areaCopies[2] = {};
        if (!enabled || done)
            return;
        PetariNative::HostAllocationScope host;
        auto finish = [&](const char* message) {
            std::fprintf(stderr, "[editor-array] %s\n", message);
            done = true;
            A::Input in;
            in.cancel = true;
            A::send(in);
            PetariNative::EditorSettings::requestLeave();
        };
        if (frame < 0) {
            if (!PetariNative::SmokePhase::stageChecksDone.load())
                return;
            PetariNative::EditorSettings::toggle();
            frame = 0;
            return;
        }
        if (frame == 0 && mode() != Mode::Editing) {
            if (++wait > 600)
                finish("FAIL: Edit never started");
            return;
        }
        const int f = ++frame;
        const auto* m = model();
        if (f == 5) {
            if (!m)
                return finish("FAIL: no model");
            float nearest = 1e30f;
            const auto* mario = MR::getPlayerPos();
            forEachBoundRow([&](RowId row, LiveActor* actor) {
                const char* name = m->objectName(row);
                if (!name || std::string(name) != "Coin" || !actor || MR::isDead(actor))
                    return;
                const float distance = mario ? (actor->mPosition - *mario).length() : 0;
                if (distance < nearest) {
                    source = row;
                    original = actor;
                    nearest = distance;
                }
            });
            if (!original)
                return finish("FAIL: no live Coin fixture");
            originalPosition = original->mPosition;
            serial = Hooks::sceneSerial();
            cursor = m->historyCursor();
            Pick::selectRow(source.v, Pick::Click::Replace);
        }
        if (f == 8)
            PetariNative::EditorSettings::frame();
        if (f == 10) {
            PetariNative::EditorSettings::dolly(-4);
            A::Parameters p;
            p.count = 6;
            p.spacing = 160;
            A::configure(p);
            A::Input in;
            in.begin = true;
            A::send(in);
        }
        if (f == 20) {
            if (!A::view().active || A::view().copies.size() != 6 || m->historyCursor() != cursor)
                return finish("FAIL: preview changed model or wrong count");
            if ((original->mPosition - originalPosition).length() > 0.01f)
                return finish("FAIL: preview moved source actor");
            for (const auto& copy : A::view().copies) {
                TVec3f gravity(0, 0, 0);
                if (MR::calcGravityVectorOrZero(nullptr, TVec3f(copy.position.x, copy.position.y, copy.position.z), &gravity, nullptr, 0)) {
                    const auto up = G::normalized(G::V3{-gravity.x, -gravity.y, -gravity.z});
                    if (G::dot(copy.basis.y, up) < 0.97f)
                        return finish("FAIL: copy not aligned to its own gravity");
                }
            }
            std::fprintf(stderr, "[editor-array] line ghosts leave source/model untouched PASS\n");
            PetariNative::EfbDump::mark("array-line");
        }
        if (f == 40) {
            auto p = A::parameters();
            p.pattern = A::Pattern::Grid;
            p.rows = 2;
            p.columns = 3;
            A::configure(p);
        }
        if (f == 45 && A::view().copies.size() != 6)
            return finish("FAIL: grid preview");
        if (f == 50) {
            auto p = A::parameters();
            p.pattern = A::Pattern::Arc;
            p.radius = 350;
            p.count = 5;
            A::configure(p);
        }
        if (f == 55 && A::view().copies.size() != 5)
            return finish("FAIL: arc preview");
        if (f == 60) {
            auto p = A::parameters();
            p.pattern = A::Pattern::Ring;
            p.count = 8;
            p.facing = A::Facing::Outward;
            A::configure(p);
        }
        if (f == 70) {
            if (A::view().copies.size() != 8)
                return finish("FAIL: ring preview");
            PetariNative::EfbDump::mark("array-ring");
        }
        if (f == 90) {
            auto p = A::parameters();
            p.pattern = A::Pattern::Path;
            p.count = 6;
            p.groundSnap = false;
            p.facing = A::Facing::Along;
            float nearest = 1e30f;
            for (const auto& rail : Paths::collect(*m)) {
                const auto line = A::railPolyline(rail, m->zoneWorldMatrix(rail.zone));
                if (line.length() < 1)
                    continue;
                const float distance = G::length(line.points.front() - G::V3{originalPosition.x, originalPosition.y, originalPosition.z});
                if (distance < nearest) {
                    p.rail = rail.row;
                    nearest = distance;
                }
            }
            if (!p.rail.valid())
                return finish("FAIL: no path fixture");
            A::configure(p);
        }
        if (f == 100) {
            if (A::view().copies.size() != 6 || A::view().pathLength < 1)
                return finish("FAIL: path preview");
            std::fprintf(stderr, "[editor-array] all five layouts preview without model edits PASS\n");
        }
        if (f == 110) {
            auto p = A::parameters();
            p.pattern = A::Pattern::Ring;
            p.count = 6;
            p.groundSnap = true;
            p.facing = A::Facing::Along;
            A::configure(p);
        }
        if (f == 120) {
            expected = A::view().copies;
            if (expected.size() != 6)
                return finish("FAIL: final preview count");
            A::Input in;
            in.apply = true;
            A::send(in);
        }
        if (f == 135) {
            if (A::view().active || m->historyCursor() != cursor + 1 || Hooks::sceneSerial() != serial)
                return finish("FAIL: commit not one live step");
            const auto selection = Pick::selection();
            if (selection.size() != 6)
                return finish("FAIL: copies not selected");
            for (size_t i = 0; i < selection.size(); ++i) {
                const RowId row{selection[i].row};
                auto* actor = actorForRow(row);
                if (!actor || row == source)
                    return finish("FAIL: copy not created");
                const auto p = expected[i].position;
                if ((actor->mPosition - TVec3f(p.x, p.y, p.z)).length() > 2)
                    return finish("FAIL: created copy differs from preview");
                copies.push_back(row);
                actors.push_back(actor);
            }
            if ((original->mPosition - originalPosition).length() > 0.01f)
                return finish("FAIL: commit moved source");
            PetariNative::EfbDump::mark("array-applied");
            std::fprintf(stderr, "[editor-array] six live copies match ground-snapped previews, one history step PASS\n");
        }
        if (f == 155)
            enqueue(Command{Command::Kind::Undo});
        if (f == 165) {
            for (size_t i = 0; i < copies.size(); ++i)
                if (m->locate(copies[i], nullptr, nullptr) || !MR::isDead(actors[i]) || EditorScene::isConnectedToDraw(actors[i]))
                    return finish("FAIL: undo leaves a copy alive/drawn");
            if (m->historyCursor() != cursor)
                return finish("FAIL: undo history");
            enqueue(Command{Command::Kind::Redo});
        }
        if (f == 180) {
            for (auto row : copies)
                if (!actorForRow(row) || !m->locate(row, nullptr, nullptr))
                    return finish("FAIL: redo");
            std::fprintf(stderr, "[editor-array] undo retires all copies; redo restores stable IDs PASS\n");
        }
        if (f == 185) {
            RowId area;
            for (const auto& volume : Areas::collect(*m)) {
                if (std::string(volume.definition->name) == "DeathCube" && m->zonePlaced(volume.zone) &&
                    (volume.missions & (1u << (m->scenario() - 1)))) {
                    area = volume.row;
                    break;
                }
            }
            if (!area.valid())
                return finish("FAIL: no area-copy fixture");
            areaManager = MR::getAreaObjManager("DeathArea");
            if (!areaManager)
                return finish("FAIL: no death-area manager");
            areaCount = areaManager->getNumAreaObj();
            Pick::selectRow(area.v, Pick::Click::Replace);
        }
        if (f == 195) {
            A::Parameters p;
            p.count = 2;
            p.groundSnap = false;
            A::configure(p);
            A::Input in;
            in.begin = true;
            A::send(in);
        }
        if (f == 205) {
            A::Input in;
            in.apply = true;
            A::send(in);
        }
        if (f == 215) {
            if (areaManager->getNumAreaObj() != areaCount + 2)
                return finish("FAIL: area copies not created live");
            for (int i = 0; i < 2; ++i) {
                areaCopies[i] = areaManager->getAreaObj(areaCount + i);
                if (!areaCopies[i]->isValid())
                    return finish("FAIL: added area inactive");
            }
            enqueue(Command{Command::Kind::Undo});
        }
        if (f == 225) {
            for (auto* area : areaCopies)
                if (area->isValid())
                    return finish("FAIL: undone area insertion still active");
            enqueue(Command{Command::Kind::Redo});
        }
        if (f == 235) {
            if (areaManager->getNumAreaObj() != areaCount + 2)
                return finish("FAIL: area redo consumed new manager slots");
            for (auto* area : areaCopies)
                if (!area->isValid())
                    return finish("FAIL: area redo did not reactivate");
            enqueue(Command{Command::Kind::Undo});
            std::fprintf(stderr, "[editor-array] actorless area copies deactivate on undo and reuse slots on redo PASS\n");
        }
        if (f == 250)
            finish("PASS");
    }
}  // namespace PetariEditor::Runtime
