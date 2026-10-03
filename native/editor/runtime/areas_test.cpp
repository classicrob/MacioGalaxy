#include "Game/AreaObj/AreaObj.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraTargetHolder.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "runtime_core.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <petari/editor/areas.hpp>
#include <petari/editor/camera_preview.hpp>
#include <petari/editor/runtime.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_pick.hpp>
#include <petari/editor_settings.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/host_allocation.hpp>
#include <petari/screenshot.hpp>
#include <petari/smoke_phase.hpp>

namespace PetariEditor::Runtime {
    void areasTestTick() {
        static const char* output = std::getenv("PETARI_EDITOR_AREAS_TEST");
        static int frame = -1, wait = 0;
        static bool done = false;
        static RowId areaRow, cameraRow;
        static AreaObj* area = nullptr;
        static Vec3 originalCenter;
        static std::uint32_t serial = 0;
        static int cursor = 0, pending = 0;
        static CameraTargetObj* originalTarget = nullptr;
        auto finish = [&](const char* result) {
            std::fprintf(stderr, "[editor-areas] %s\n", result);
            done = true;
            PetariNative::EditorSettings::requestLeave();
        };
        if (!output || done)
            return;
        PetariNative::HostAllocationScope host;
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
            serial = Hooks::sceneSerial();
            cursor = m->historyCursor();
            pending = status().pendingCount;
            for (const auto& placed : Hooks::areas()) {
                const auto row = rowForPlacement(placed.table, placed.row);
                if (!row.valid())
                    continue;
                const auto volume = Areas::describe(*m, row);
                if (!volume)
                    continue;
                auto* obj = static_cast< AreaObj* >(placed.actor);
                if (!area && std::string(volume->definition->manager) == "DeathArea" && !obj->getFollowMtx() && obj->isValid()) {
                    areaRow = row;
                    area = obj;
                    originalCenter = Areas::center(*volume);
                }
                if (!cameraRow.valid() && volume->definition->kind == Areas::Kind::Camera) {
                    const auto param = Areas::cameraRow(*m, *volume);
                    const auto* type = m->cell(param, columnHash("camtype"));
                    if (type && (type->s == "CAM_TYPE_XZ_PARA" || type->s == "CAM_TYPE_POINT_FIX"))
                        cameraRow = row;
                }
            }
            if (!area || !cameraRow.valid())
                return finish("FAIL: no death area / preview camera fixture");
            originalTarget = MR::getCameraDirector()->mTargetHolder->mTarget;
            Areas::setVisibility(false, Areas::Visibility::Selection);
            PetariNative::EditorPick::selectRow(areaRow.v, PetariNative::EditorPick::Click::Replace);
        }
        if (f == 10)
            PetariNative::EditorSettings::frame();
        if (f == 20) {
            Command c{Command::Kind::Apply};
            c.op.label = "Move and resize live area";
            for (const auto* key : {"pos_x", "scale_x"}) {
                Cell value = *m->cell(areaRow, columnHash(key));
                value.f += std::string(key) == "pos_x" ? 800.f : 1.f;
                c.op.sets.push_back({areaRow, columnHash(key), value});
            }
            enqueue(std::move(c));
        }
        if (f == 30) {
            const auto volume = Areas::describe(*m, areaRow);
            const auto center = Areas::center(*volume);
            if (!area->isInVolume(TVec3f(center.x, center.y, center.z)))
                return finish("FAIL: area form not moved live");
            const auto outside = DebugDraw::point(DebugDraw::vector(volume->origin) + volume->axes.x * (volume->size.x + 10));
            if (area->isInVolume(TVec3f(outside.x, outside.y, outside.z)))
                return finish("FAIL: area size not applied");
            if (Hooks::sceneSerial() != serial || status().pendingCount != pending || m->historyCursor() != cursor + 1)
                return finish("FAIL: area commit pending/reloaded/history mismatch");
            PetariNative::EfbDump::mark("areas-edited");
        }
        if (f == 40) {
            const auto file = std::string(output) + "/areas-edited.png";
            PetariNative::Screenshot::request(file.c_str());
            enqueue(Command{Command::Kind::Undo});
        }
        if (f == 50) {
            if (!area->isInVolume(TVec3f(originalCenter.x, originalCenter.y, originalCenter.z)) || m->historyCursor() != cursor)
                return finish("FAIL: area undo");
            Command c{Command::Kind::Apply};
            c.op.label = "Delete area";
            c.op.deletes.push_back(areaRow);
            enqueue(std::move(c));
        }
        if (f == 60) {
            if (area->isValid())
                return finish("FAIL: deleted area still active");
            enqueue(Command{Command::Kind::Undo});
        }
        if (f == 70) {
            if (!area->isValid())
                return finish("FAIL: undo did not reactivate area");
            std::fprintf(stderr, "[editor-areas] live transform, size, delete and undo PASS\n");
            PetariNative::EditorPick::selectRow(cameraRow.v, PetariNative::EditorPick::Click::Replace);
        }
        if (f == 80)
            PetariNative::EditorSettings::frame();
        if (f == 90) {
            Command c{Command::Kind::Apply};
            c.op.label = "Edit camera priority";
            Cell value = *m->cell(cameraRow, columnHash("Obj_arg2"));
            ++value.i;
            c.op.sets.push_back({cameraRow, columnHash("Obj_arg2"), value});
            enqueue(std::move(c));
        }
        if (f == 110) {
            if (status().pendingCount != pending + 1 || Hooks::sceneSerial() != serial)
                return finish("FAIL: camera edit not pending");
            std::fprintf(stderr, "[editor-areas] camera edits pending, scene unchanged PASS\n");
        }
        if (f == 180) {
            const auto& preview = CameraPreview::view();
            if (preview.area != cameraRow || preview.serial == 0 || !std::isfinite(preview.eye.x) ||
                Gizmo::length(DebugDraw::vector(preview.eye) - DebugDraw::vector(preview.target)) < 1)
                return finish("FAIL: camera preview has no finite pose");
            if (MR::getCameraDirector()->mTargetHolder->mTarget != originalTarget)
                return finish("FAIL: preview changed active camera target");
            PetariNative::EfbDump::mark("areas-camera-preview");
            std::fprintf(stderr, "[editor-areas] camera preview pose finite and active target restored PASS\n");
        }
        if (f == 200) {
            const auto file = std::string(output) + "/camera-preview.png";
            PetariNative::Screenshot::request(file.c_str());
        }
        if (f == 230)
            finish("PASS");
    }
}  // namespace PetariEditor::Runtime
