#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "runtime_core.hpp"
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/host_allocation.hpp>
#include <petari/smoke_phase.hpp>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace PetariEditor::Runtime {
    void placementTestTick() {
        namespace Pick = PetariNative::EditorPick;
        namespace Settings = PetariNative::EditorSettings;
        static const bool enabled = std::getenv("PETARI_EDITOR_PLACEMENT_TEST") != nullptr;
        static bool done = false;
        static int frame = -1, wait = 0, cursor = 0;
        static RowId donor, clean, kept, launcher;
        static int launchZone = 0, launchRail = -1;
        static std::map< std::string, int > donorLinks;
        static LiveActor* placedActor = nullptr;
        static std::uint32_t serial = 0;
        if (!enabled || done)
            return;
        PetariNative::HostAllocationScope host;
        auto finish = [&](const char* message) {
            std::fprintf(stderr, "[editor-placement] %s\n", message);
            done = true;
        };
        if (frame < 0) {
            if (!PetariNative::SmokePhase::stageChecksDone.load())
                return;
            Settings::requestEnter();
            frame = 0;
            return;
        }
        if (mode() != Mode::Editing || !model()) {
            if (++wait > 600)
                finish("FAIL: Edit did not start");
            return;
        }
        const int f = ++frame;
        const auto& m = *model();
        auto selected = [&]() -> RowId {
            const auto rows = Pick::selection();
            return rows.empty() ? RowId{} : RowId{rows.front().row};
        };
        auto value = [&](RowId row, const char* field) {
            const auto* cell = m.cell(row, columnHash(field));
            return cell ? cell->i : -9999;
        };
        if (f == 5) {
            for (int t = 0; t < m.tableCount(0) && !donor.valid(); ++t) {
                m.forEachRow(0, t, [&](RowId row) {
                    const char* name = m.objectName(row);
                    if (!donor.valid() && m.state(row) != RowState::Deleted && name && std::string(name) == "BenefitItemOneUp")
                        donor = row;
                });
            }
            if (!donor.valid())
                return finish("FAIL: no main-zone 1-Up donor");
            Command fixture{Command::Kind::Apply};
            fixture.op.label = "Prepare hidden donor fixture";
            for (const char* field : {"SW_APPEAR", "SW_SLEEP", "GroupId", "ViewGroupId", "ClippingGroupId", "CameraSetId"})
                donorLinks[field] = value(donor, field);
            donorLinks["SW_APPEAR"] = donorLinks["SW_SLEEP"] = 75;
            for (const char* field : {"SW_APPEAR", "SW_SLEEP"})
                fixture.op.sets.push_back({donor, columnHash(field), {CellType::Int, 75, 0, {}}});
            enqueue(std::move(fixture));
        }
        if (f == 10) {
            cursor = m.historyCursor();
            serial = Hooks::sceneSerial();
            Settings::cursor(0, 0);
            Pick::place(donor.v);
        }
        if (f == 20) {
            clean = selected();
            if (!clean.valid() || clean == donor || m.historyCursor() != cursor + 1)
                return finish("FAIL: Browser request did not place one copy");
            for (const char* field : {"SW_APPEAR", "SW_SLEEP", "GroupId", "ViewGroupId", "ClippingGroupId", "CameraSetId"}) {
                if (value(clean, field) != -1 || value(donor, field) != donorLinks[field])
                    return finish("FAIL: optional links leaked or donor was modified");
            }
            if (value(clean, "l_id") == value(donor, "l_id"))
                return finish("FAIL: copy reused donor identity");
            placedActor = actorForRow(clean);
            if (!placedActor || MR::isDead(placedActor) || MR::isHiddenModel(placedActor) || !MR::getJ3DModel(placedActor))
                return finish("FAIL: independent 1-Up is hidden");
            std::fprintf(stderr, "[editor-placement] actual Browser request -> visible 1-Up, clean links, donor unchanged PASS\n");
            Settings::frame();
        }
        if (f == 23)
            Settings::dolly(-4);
        if (f == 28)
            PetariNative::EfbDump::mark("placement-clean");
        if (f == 45)
            enqueue(Command{Command::Kind::Undo});
        if (f == 50) {
            if (m.locate(clean, nullptr, nullptr) || !MR::isDead(placedActor) || EditorScene::isConnectedToDraw(placedActor))
                return finish("FAIL: undo left placed actor active");
            enqueue(Command{Command::Kind::Redo});
        }
        if (f == 55) {
            auto* actor = actorForRow(clean);
            if (!actor || MR::isDead(actor) || value(clean, "SW_APPEAR") != -1 || m.historyCursor() != cursor + 1)
                return finish("FAIL: redo did not restore the independent copy");
            Settings::cursor(0, 0);
            Pick::place(donor.v, true);
        }
        if (f == 65) {
            kept = selected();
            if (!kept.valid() || kept == clean || kept == donor || m.historyCursor() != cursor + 2)
                return finish("FAIL: Keep links did not place a second copy");
            for (const char* field : {"SW_APPEAR", "SW_SLEEP", "GroupId", "ViewGroupId", "ClippingGroupId", "CameraSetId"})
                if (value(kept, field) != donorLinks[field])
                    return finish("FAIL: Keep links changed donor wiring");
            if (Hooks::sceneSerial() != serial)
                return finish("FAIL: placement reloaded the scene");
            std::fprintf(stderr, "[editor-placement] one-step undo/redo and explicit Keep links PASS\n");
            enqueue(Command{Command::Kind::Undo});
            Pick::selectRow(clean.v, Pick::Click::Replace);
        }
        if (f == 75) {
            for (int z = 0; z < m.zoneCount() && !launcher.valid(); ++z) {
                if (!m.zonePlaced(z))
                    continue;
                for (int t = 0; t < m.tableCount(z) && !launcher.valid(); ++t) {
                    m.forEachRow(z, t, [&](RowId row) {
                        const char* name = m.objectName(row);
                        if (!launcher.valid() && m.state(row) != RowState::Deleted && name && std::string(name) == "SuperSpinDriver" &&
                            (m.rowMissions(row) & (1u << (m.scenario() - 1))) && value(row, "CommonPath_ID") >= 0) {
                            launcher = row;
                            launchZone = z;
                            launchRail = value(row, "CommonPath_ID");
                        }
                    });
                }
            }
            if (!launcher.valid())
                return finish("FAIL: no active Launch Star donor");
            Command fixture{Command::Kind::Apply};
            fixture.op.label = "Prepare hidden Launch Star fixture";
            fixture.op.sets.push_back({launcher, columnHash("SW_APPEAR"), {CellType::Int, 1007, 0, {}}});
            enqueue(std::move(fixture));
        }
        if (f == 85) {
            Settings::cursor(0, 0);
            Pick::place(launcher.v);
        }
        if (f == 95) {
            const auto copy = selected();
            int zone = -1;
            if (!copy.valid() || copy == launcher || copy == clean || !m.locate(copy, &zone, nullptr) || zone != launchZone ||
                value(copy, "CommonPath_ID") != launchRail || value(copy, "SW_APPEAR") != -1 || value(launcher, "SW_APPEAR") != 1007)
                return finish("FAIL: Launch Star lost required path or kept hidden switch");
            const auto* actor = actorForRow(copy);
            if (!actor || MR::isDead(actor) || MR::isHiddenModel(actor))
                return finish("FAIL: independent Launch Star is hidden");
            std::fprintf(stderr, "[editor-placement] visible Launch Star clears SW_APPEAR1007; required path and zone retained PASS\n");
            Settings::frame();
        }
        if (f == 98)
            Settings::dolly(-4);
        if (f == 105)
            PetariNative::EfbDump::mark("placement-launch");
        if (f == 120)
            return finish("PASS");
    }
}  // namespace PetariEditor::Runtime
