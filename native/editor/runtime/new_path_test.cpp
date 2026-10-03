#include "Game/Camera/EditorPaths.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "model_io.hpp"
#include "runtime_core.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <petari/editor/paths_view.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/host_allocation.hpp>
#include <petari/smoke_phase.hpp>

namespace PetariEditor::Runtime {
    void newPathTestTick() {
        namespace V = PathsView;
        namespace D = DebugDraw;
        namespace G = Gizmo;
        static const char* output = std::getenv("PETARI_EDITOR_NEW_PATH_TEST");
        static int frame = -1, wait = 0, cursor = 0;
        static bool done = false;
        static RowId rail, star;
        static size_t initialCount = 0;
        static std::uint32_t serial = 0;
        static EditorPicking::View camera;
        if (!output || done) {
            return;
        }
        PetariNative::HostAllocationScope host;
        auto finish = [&](const std::string& message) {
            std::fprintf(stderr, "[editor-new-path] %s\n", message.c_str());
            done = true;
            V::endCreate();
            PetariNative::EditorSettings::requestLeave();
        };
        if (frame < 0) {
            if (!PetariNative::SmokePhase::stageChecksDone.load()) {
                return;
            }
            PetariNative::EditorSettings::toggle();
            frame = 0;
            return;
        }
        if (mode() != Mode::Editing || !model()) {
            if (++wait > 800) {
                finish("FAIL: Edit did not start");
            }
            return;
        }
        const int f = ++frame;
        const auto* m = model();
        auto send = [&](V::CreateInput input) {
            V::sendCreate(input);
            EditorPaths::update(camera);
        };
        auto click = [&](float x) {
            V::CreateInput input;
            input.hover = input.add = true;
            input.x = x;
            send(input);
        };
        if (f == 5) {
            const auto* mario = MR::getPlayerPos();
            if (!mario) {
                return finish("FAIL: no player");
            }
            TVec3f gravity;
            MR::calcGravityVector(nullptr, *mario, &gravity, nullptr, 0);
            const auto up = G::normalized({-gravity.x, -gravity.y, -gravity.z}, {0, 1, 0});
            const auto basis = G::gravityFrame(up, {0, 0, -1});
            const auto eye = G::V3{mario->x, mario->y, mario->z} + up * 800.0f;
            camera = {{eye.x, eye.y, eye.z}, {-up.x, -up.y, -up.z}, {basis.z.x, basis.z.y, basis.z.z}, 45, 1, 0, 0};
            initialCount = Paths::collect(*m).size();
            cursor = m->historyCursor();
            serial = Hooks::sceneSerial();
            V::beginCreate();
            click(-0.3f);
            click(0);
            click(0.3f);
            if (V::view().draftPoints != 3 || m->historyCursor() != cursor) {
                return finish("FAIL: three ground clicks did not make a pure draft");
            }
            V::CreateInput cancel;
            cancel.cancel = true;
            send(cancel);
            if (V::creating() || Paths::collect(*m).size() != initialCount || m->historyCursor() != cursor) {
                return finish("FAIL: cancellation touched the model");
            }
            V::beginCreate();
            click(-0.3f);
            click(0);
            click(0.3f);
            PetariNative::EfbDump::mark("new-path-draft");
        }
        if (f == 10) {
            V::CreateInput finishInput;
            finishInput.finish = true;
            send(finishInput);
        }
        if (f == 15) {
            const auto rails = Paths::collect(*m);
            if (rails.size() != initialCount + 1 || m->historyCursor() != cursor + 1) {
                return finish("FAIL: creation was not one undo step");
            }
            for (const auto& item : rails) {
                if (m->state(item.row) == RowState::Added) {
                    rail = item.row;
                }
            }
            if (!rail.valid() || m->railPoints(rail).size() != 3) {
                return finish("FAIL: missing three-point rail");
            }
            PetariNative::EditorPick::selectRow(rail.v, PetariNative::EditorPick::Click::Replace);
            PetariNative::EditorSettings::frame();
        }
        if (f == 35) {
            PetariNative::EfbDump::mark("new-path-created");
        }
        if (f == 55) {
            enqueue(Command{Command::Kind::Undo});
        }
        if (f == 65) {
            if (Paths::collect(*m).size() != initialCount || m->historyCursor() != cursor) {
                return finish("FAIL: undo left path rows");
            }
            enqueue(Command{Command::Kind::Redo});
        }
        if (f == 75) {
            if (m->railPoints(rail).size() != 3 || Hooks::sceneSerial() != serial) {
                return finish("FAIL: redo/reload");
            }
            std::fprintf(stderr, "[editor-new-path] three ground clicks, cancel, one-op creation and undo/redo PASS\n");
            const auto first = Paths::collect(*m);
            int zone = 0;
            Vec3 start;
            for (const auto& item : first) {
                if (item.row == rail) {
                    zone = item.zone;
                    start = item.points.front().position;
                }
            }
            star = newRowId();
            Command add{Command::Kind::Apply};
            add.op.label = "Place Launch Star fixture";
            AddRow row;
            row.id = star;
            row.zone = zone;
            row.table = "jmp/placement/common/objinfo";
            row.cells = {{columnHash("name"), Cell{CellType::StringOffset, 0, 0, "SuperSpinDriver"}},
                         {columnHash("pos_x"), Cell{CellType::Float, 0, start.x, {}}},
                         {columnHash("pos_y"), Cell{CellType::Float, 0, start.y, {}}},
                         {columnHash("pos_z"), Cell{CellType::Float, 0, start.z, {}}},
                         {columnHash("Obj_arg0"), Cell{CellType::Int, 120, 0, {}}}};
            add.op.adds.push_back(row);
            enqueue(std::move(add));
        }
        if (f == 85) {
            cursor = m->historyCursor();
            V::beginCreate(star);
            click(0.3f);
            if (!V::view().flight || V::view().draftPoints != 2) {
                return finish("FAIL: star start plus landing");
            }
            V::CreateInput input;
            input.finish = true;
            send(input);
        }
        if (f == 95) {
            const auto* link = m->cell(star, columnHash("CommonPath_ID"));
            bool found = false;
            for (const auto& item : Paths::collect(*m)) {
                if (link && item.id == link->i && item.points.size() == 2) {
                    found = true;
                }
            }
            if (!found || m->historyCursor() != cursor + 1) {
                return finish("FAIL: flight path and link not atomic");
            }
            enqueue(Command{Command::Kind::Undo});
        }
        if (f == 105) {
            const auto* link = m->cell(star, columnHash("CommonPath_ID"));
            if ((link && link->i >= 0) || Paths::collect(*m).size() != initialCount + 1) {
                return finish("FAIL: flight undo left link or path");
            }
            enqueue(Command{Command::Kind::Redo});
        }
        if (f == 115) {
            const auto* edit = dynamic_cast< const EditModel* >(m);
            std::string error, summary;
            if (!edit || !ModelIO::saveToDirectory(*edit, std::filesystem::path(output) / "user/mods/NewPathFixture", false, &summary, &error)) {
                return finish("FAIL: patch save " + error);
            }
            if (Hooks::sceneSerial() != serial) {
                return finish("FAIL: draft caused reload");
            }
            finish("PASS: new flight path+link, undo/redo and patch save");
        }
    }
}  // namespace PetariEditor::Runtime
