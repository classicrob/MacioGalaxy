#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "runtime_core.hpp"
#include <petari/editor/model_ops.hpp>
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
    void missionsTestTick() {
        namespace Pick = PetariNative::EditorPick;
        namespace Settings = PetariNative::EditorSettings;
        static const bool enabled = std::getenv("PETARI_EDITOR_MISSIONS_TEST") != nullptr;
        static bool done = false;
        static int frame = -1, wait = 0, cursor = 0;
        static RowId donor, common, moved;
        static std::vector< RowId > split;
        static LiveActor* oldActor = nullptr;
        static std::uint32_t serial = 0, allMissions = 0, repairMask = 0, splitMask = 0;
        static std::map< std::uint32_t, Cell > saved;
        if (!enabled || done)
            return;
        PetariNative::HostAllocationScope host;
        auto finish = [&](const std::string& message) {
            std::fprintf(stderr, "[editor-missions] %s\n", message.c_str());
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
        auto capacityError = [&]() {
            for (const auto& check : checks())
                if (check.key == "launch-stars:2" && check.severity == Severity::Error)
                    return true;
            return false;
        };
        auto sameCells = [&](RowId row) {
            for (const auto& [column, before] : saved) {
                const auto* after = m.cell(row, column);
                if (!after || before.type != after->type || before.i != after->i || before.f != after->f || before.s != after->s)
                    return false;
            }
            return true;
        };
        if (f == 5) {
            if (m.scenario() != 1 || std::string(m.galaxy()) != "EggStarGalaxy")
                return finish("FAIL: requires Good Egg mission 1");
            for (int t = 0; t < m.tableCount(0) && !donor.valid(); ++t) {
                if (std::string(m.tablePath(0, t)) != "jmp/placement/common/objinfo")
                    continue;
                m.forEachRow(0, t, [&](RowId row) {
                    const char* name = m.objectName(row);
                    const auto* rail = m.cell(row, columnHash("CommonPath_ID"));
                    if (!donor.valid() && m.state(row) != RowState::Deleted && name && std::string(name) == "SuperSpinDriver" && rail && rail->i >= 0)
                        donor = row;
                });
            }
            if (!donor.valid() || capacityError())
                return finish("FAIL: baseline Launch Star fixture unavailable");
            repairMask = ModelOps::layerMissions(m, 0, 1);
            splitMask = repairMask | ModelOps::layerMissions(m, 0, 2);
            if (!(repairMask & 1) || (repairMask & 2) || !(splitMask & 2))
                return finish("FAIL: expected Good Egg mission masks unavailable");
            serial = Hooks::sceneSerial();
            Settings::cursor(0, 0);
            Pick::place(donor.v);
        }
        if (f == 20) {
            const auto selection = Pick::selection();
            common = selection.empty() ? RowId{} : RowId{selection.front().row};
            int zone = -1, table = -1;
            if (!common.valid() || common == donor || !m.locate(common, &zone, &table) || zone != 0 || !capacityError())
                return finish("FAIL: common copy did not create mission 2 capacity error");
            allMissions = m.rowMissions(common);
            for (const auto& column : m.columns(zone, table))
                if (const auto* cell = m.cell(common, column.hash))
                    saved[column.hash] = *cell;
            oldActor = actorForRow(common);
            if (!oldActor || MR::isDead(oldActor))
                return finish("FAIL: common copy is not live");
            cursor = m.historyCursor();
            Settings::frame();
            std::fprintf(stderr, "[editor-missions] common Launch Star exposes mission 2 capacity error PASS\n");
        }
        if (f == 23)
            Settings::dolly(-4);
        if (f == 30)
            PetariNative::EfbDump::mark("missions-common");
        if (f == 45) {
            const auto impossible = ModelOps::changeMissions(m, {common}, 1u);
            if (impossible.error.empty() || !impossible.op.adds.empty() || !impossible.op.deletes.empty())
                return finish("FAIL: Good Egg mission1-only should explain unavailable layers");
            std::fprintf(stderr, "[editor-missions] unavailable exact membership explained: %s\n", impossible.error.c_str());
            auto result = ModelOps::changeMissions(m, {common}, repairMask);
            if (!result.error.empty() || result.rows.size() != 1 || result.op.adds.size() != 1 || result.op.deletes.size() != 1)
                return finish("FAIL: mission repair op: " + result.error);
            moved = result.rows.front();
            Command command{Command::Kind::Apply};
            command.op = std::move(result.op);
            enqueue(std::move(command));
            Pick::selectRow(moved.v, Pick::Click::Replace);
        }
        if (f == 60) {
            auto* actor = actorForRow(moved);
            if (m.historyCursor() != cursor + 1 || m.rowMissions(moved) != repairMask || m.state(common) != RowState::Deleted || !sameCells(moved) ||
                capacityError() || !actor || MR::isDead(actor) || !MR::isDead(oldActor) || EditorScene::isConnectedToDraw(oldActor)) {
                std::fprintf(
                    stderr,
                    "[editor-missions] repair detail history=%d/%d mask=%u/%u oldState=%d cells=%d cap=%d actor=%p alive=%d oldDead=%d oldDraw=%d\n",
                    m.historyCursor(), cursor + 1, m.rowMissions(moved), repairMask, static_cast< int >(m.state(common)), sameCells(moved),
                    capacityError(), static_cast< void* >(actor), actor && !MR::isDead(actor), MR::isDead(oldActor),
                    EditorScene::isConnectedToDraw(oldActor));
                return finish("FAIL: mission repair move did not preserve cells/identity, retire old actor, or clear capacity");
            }
            if (Hooks::sceneSerial() != serial)
                return finish("FAIL: mission edit reloaded the scene");
            std::fprintf(stderr, "[editor-missions] one-step move preserves all cells and l_id; old actor retired; mission 2 error cleared PASS\n");
        }
        if (f == 70)
            PetariNative::EfbDump::mark("missions-repaired");
        if (f == 90)
            enqueue(Command{Command::Kind::Undo});
        if (f == 105) {
            if (m.historyCursor() != cursor || m.rowMissions(common) != allMissions || m.locate(moved, nullptr, nullptr) || !sameCells(common) ||
                !capacityError())
                return finish("FAIL: undo did not restore common membership and capacity error");
            enqueue(Command{Command::Kind::Redo});
        }
        if (f == 120) {
            if (m.historyCursor() != cursor + 1 || m.rowMissions(moved) != repairMask || !sameCells(moved) || capacityError() ||
                Hooks::sceneSerial() != serial)
                return finish("FAIL: redo did not restore mission repair membership");
            const auto empty = ModelOps::changeMissions(m, {moved}, 0);
            if (empty.error.empty() || !empty.op.deletes.empty() || !empty.op.adds.empty())
                return finish("FAIL: an empty mission choice would remove the object");
            std::fprintf(stderr, "[editor-missions] undo restores capacity warning; redo repairs it; empty membership rejected PASS\n");
            Pick::selectRow(moved.v, Pick::Click::Replace);
        }
        if (f == 125) {
            auto result = ModelOps::changeMissions(m, {moved}, splitMask);
            if (!result.error.empty() || result.rows.size() != 2)
                return finish("FAIL: disjoint split: " + result.error);
            split = result.rows;
            Command command{Command::Kind::Apply};
            command.op = std::move(result.op);
            enqueue(std::move(command));
            for (size_t i = 0; i < split.size(); ++i)
                Pick::selectRow(split[i].v, i ? Pick::Click::Add : Pick::Click::Replace);
        }
        if (f == 140) {
            std::uint32_t mask = 0;
            int live = 0;
            for (const auto row : split) {
                const auto rowMask = m.rowMissions(row);
                if (!sameCells(row) || (mask & rowMask))
                    return finish("FAIL: split changes settings or overlaps missions");
                mask |= rowMask;
                if (const auto* actor = actorForRow(row); actor && !MR::isDead(actor))
                    ++live;
            }
            if (mask != splitMask || live != 1 || !capacityError() || m.historyCursor() != cursor + 2 || Pick::selection().size() != 2)
                return finish("FAIL: split does not preserve membership, actor count, selection or capacity");
            std::fprintf(stderr, "[editor-missions] disjoint split retains cells/l_id; one current-mission actor; both rows selected PASS\n");
            PetariNative::EfbDump::mark("missions-split");
        }
        if (f == 160) {
            auto result = ModelOps::changeMissions(m, {split.front()}, repairMask);
            if (!result.error.empty() || result.rows.size() != 1)
                return finish("FAIL: coalesce matching unselected sibling: " + result.error);
            moved = result.rows.front();
            Command command{Command::Kind::Apply};
            command.op = std::move(result.op);
            enqueue(std::move(command));
            Pick::selectRow(moved.v, Pick::Click::Replace);
        }
        if (f == 175) {
            int remaining = 0;
            for (const auto row : split)
                remaining += m.locate(row, nullptr, nullptr) && m.state(row) != RowState::Deleted;
            if (remaining != 1 || m.historyCursor() != cursor + 3 || !sameCells(moved) || m.rowMissions(moved) != repairMask || capacityError())
                return finish("FAIL: selecting one split row did not coalesce its matching sibling");
            enqueue(Command{Command::Kind::Undo});
        }
        if (f == 190) {
            if (!capacityError() || !m.locate(split[0], nullptr, nullptr) || !m.locate(split[1], nullptr, nullptr))
                return finish("FAIL: undo did not restore the split");
            enqueue(Command{Command::Kind::Redo});
        }
        if (f == 205) {
            if (capacityError() || m.historyCursor() != cursor + 3 || Hooks::sceneSerial() != serial)
                return finish("FAIL: coalesce redo changed capacity or scene");
            std::fprintf(stderr, "[editor-missions] single-row selection coalesces matching split; undo/redo restores it PASS\n");
            finish("PASS");
        }
    }
}  // namespace PetariEditor::Runtime
