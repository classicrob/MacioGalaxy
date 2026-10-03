#include "Game/Editor/EditorScene.hpp"
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "model_io.hpp"
#include "runtime_core.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <petari/editor/import_assets.hpp>
#include <petari/editor/runtime_hooks.hpp>
#include <petari/editor_settings.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/host_allocation.hpp>
#include <petari/smoke_phase.hpp>

namespace PetariEditor::Runtime {
    void importTestTick() {
        static const char* output = std::getenv("PETARI_EDITOR_IMPORT_TEST");
        static const bool reopen = std::getenv("PETARI_EDITOR_IMPORT_REOPEN") != nullptr;
        static int frame = -1, step = 0, wait = 0, cursor = 0;
        static bool done = false;
        static RowId row;
        static LiveActor* actor = nullptr;
        static LiveActor* visual = nullptr;
        static std::uint32_t serial = 0;
        if (!output || done) {
            return;
        }
        PetariNative::HostAllocationScope host;
        auto finish = [&](const std::string& text) {
            std::fprintf(stderr, "[editor-import] %s\n", text.c_str());
            done = true;
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
            if (++wait > 1000) {
                finish("FAIL: Edit did not start");
            }
            return;
        }
        ++frame;
        const auto* m = model();
        if (frame == 5) {
            cursor = m->historyCursor();
            serial = Hooks::sceneSerial();
            const auto* p = MR::getPlayerPos();
            Command marker{Command::Kind::SetPlayMarker};
            marker.markerPos = {p->x + 500, p->y + 500, p->z};
            enqueue(std::move(marker));
        }
        if (step == 0) {
            forEachBoundRow([&](RowId id, LiveActor* a) {
                if (Imports::parseClassName(m->objectName(id)) && a && !MR::isDead(a)) {
                    row = id;
                    actor = a;
                }
            });
            if (!actor) {
                if (frame > 2000) {
                    finish("FAIL: import sheet never placed a part");
                }
                return;
            }
            if (!reopen && (m->historyCursor() != cursor + 1 || Hooks::sceneSerial() != serial)) {
                return finish("FAIL: import was not one live undo step");
            }
            if (!EditorTerrain::partModel(actor) || !actor->mCollisionParts) {
                return finish("FAIL: imported model or collision missing");
            }
            TVec3f hit;
            Triangle triangle;
            const TVec3f from(actor->mPosition.x, actor->mPosition.y + 500, actor->mPosition.z);
            if (!MR::getFirstPolyOnLineToMap(&hit, &triangle, from, TVec3f(0, -1000, 0)) || !triangle.mSensor || triangle.mSensor->mHost != actor) {
                return finish("FAIL: game collision query did not hit imported mesh");
            }
            visual = static_cast< EditorTerrain::Part* >(actor)->mModel;
            PetariNative::EditorSettings::frame();
            step = 1;
            wait = 0;
            std::fprintf(stderr, "[editor-import] live textured part + collision, one op, no reload PASS\n");
        }
        if (step == 1 && ++wait == 25) {
            PetariNative::EfbDump::mark("import-placed");
            if (reopen) {
                return finish("PASS: saved patch reopened as editable textured collision part");
            }
            step = 2;
            wait = 0;
        }
        if (step == 2 && ++wait == 25) {
            Command undo{Command::Kind::Undo};
            enqueue(std::move(undo));
            step = 3;
            wait = 0;
        }
        if (step == 3 && ++wait == 5) {
            if (actorForRow(row) || !MR::isDead(actor) || !MR::isDead(visual) || EditorScene::isConnectedToDraw(visual)) {
                return finish("FAIL: undo left imported part active");
            }
            enqueue(Command{Command::Kind::Redo});
            step = 4;
            wait = 0;
        }
        if (step == 4 && ++wait == 5) {
            if (actorForRow(row) != actor || MR::isDead(actor) || !EditorTerrain::partModel(actor)) {
                return finish("FAIL: redo did not restore reusable imported part");
            }
            std::fprintf(stderr, "[editor-import] undo retires model/collision; redo reuses part PASS\n");
            std::string error, summary;
            const auto* edit = dynamic_cast< const EditModel* >(m);
            const auto root = std::filesystem::path(output) / "user" / "mods" / "ImportedFixture";
            if (!edit || !ModelIO::saveToDirectory(*edit, root, false, &summary, &error)) {
                return finish("FAIL: patch save " + error);
            }
            if (!std::filesystem::is_directory(root / "assets" / "models")) {
                return finish("FAIL: neutral assets not copied into patch mod");
            }
            if (!ModelIO::saveToDirectory(*edit, std::filesystem::path(output) / "full-mod", true, &summary, &error)) {
                return finish("FAIL: native bake " + error);
            }
            std::fprintf(stderr, "[editor-import] portable patch assets + full native planet bake PASS\n");
            finish("PASS");
        }
    }
}  // namespace PetariEditor::Runtime
