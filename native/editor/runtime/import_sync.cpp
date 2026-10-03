#include "import_sync.hpp"
#include "Game/Editor/EditorScene.hpp"
#include "Game/Editor/EditorTerrain.hpp"
#include "Game/Gravity/PlanetGravity.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "edit_model.hpp"
#include "model_io.hpp"
#include "runtime_core.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <petari/editor/import_assets.hpp>
#include <petari/editor/runtime.hpp>
#include <petari/editor_pick.hpp>
#include <petari/host_allocation.hpp>
#include <petari/kcl_collision.hpp>

namespace PetariEditor::ImportSync {
    namespace {
        namespace I = Imports;
        struct Live {
            EditorTerrain::Part* actor = nullptr;
            std::string name;
            Vec3 scale{};
            const char* archive = nullptr;
        };
        std::map< RowId, Live > gLive;
        std::vector< void* > gBuffers;
        const char* persistent(std::string name) {
            static std::deque< std::string > names;
            PetariNative::HostAllocationScope host;
            names.push_back(std::move(name));
            return names.back().c_str();
        }
        bool owns(const char* name) {
            return I::parseClassName(name);
        }
        void retire(RowId id) {
            const auto it = gLive.find(id);
            if (it == gLive.end()) {
                return;
            }
            if (it->second.actor) {
                it->second.actor->retire();
            }
            // Retain the part for undo/redo: it can reuse its gravity slot.
            Core::unbindRow(id);
        }
        void beginScene() {
            gLive.clear();
            for (void* p : gBuffers) {
                std::free(p);
            }
            gBuffers.clear();
        }
        void sync(const EditModel& model, RowId id) {
            int zone = 0;
            if (!model.locate(id, &zone, nullptr)) {
                return;
            }
            std::string assetId;
            int partIndex = 0;
            if (!I::parseClassName(model.objectName(id), &assetId, &partIndex)) {
                return;
            }
            const auto asset = I::findAsset(assetId);
            if (!asset) {
                Core::reportError("Imported model asset is missing.");
                retire(id);
                return;
            }
            const auto t = model.transform(id);
            Live* entry;
            {
                PetariNative::HostAllocationScope host;
                entry = &gLive[id];
            }
            const bool rebuild = !entry->actor || !entry->actor->mModel || entry->name != model.objectName(id) || entry->scale.x != t.scale.x ||
                                 entry->scale.y != t.scale.y || entry->scale.z != t.scale.z;
            I::BuiltPart built;
            Formats::Bytes archive;
            void* kcl = nullptr;
            void* pa = nullptr;
            const char* archiveName = nullptr;
            const char* archivePath = nullptr;
            if (rebuild) {
                PetariNative::HostAllocationScope host;
                built = I::buildPart(*asset, partIndex, {t.scale.x, t.scale.y, t.scale.z}, ModelIO::readMounted);
                if (!built.error.empty()) {
                    Core::reportError("Import: " + built.error);
                    return;
                }
                if (!built.kcl.empty()) {
                    kcl = std::malloc(built.kcl.size());
                    pa = std::malloc(built.pa.size());
                    if (!kcl || !pa) {
                        std::free(kcl);
                        std::free(pa);
                        Core::reportError("Not enough memory for imported collision.");
                        return;
                    }
                    std::memcpy(kcl, built.kcl.data(), built.kcl.size());
                    std::memcpy(pa, built.pa.data(), built.pa.size());
                    if (const char* error = PetariNative::KCL::convertInPlace(kcl, built.kcl.size())) {
                        std::free(kcl);
                        std::free(pa);
                        Core::reportError(error);
                        return;
                    }
                    gBuffers.push_back(kcl);
                    gBuffers.push_back(pa);
                }
                static unsigned version = 0;
                const auto name = "EditorImported_" + std::to_string(id.v) + "_" + std::to_string(++version);
                std::string base = name;
                for (char& c : base) {
                    c = std::tolower(static_cast< unsigned char >(c));
                }
                archive = Formats::Archive::create(base, {{base + ".bmd", built.model}}).build();
                archiveName = persistent(name);
                archivePath = persistent("/ObjectData/" + name + ".arc");
            }
            if (!entry->actor) {
                const char* error = "";
                entry->actor = EditorTerrain::createPart(persistent("Imported_" + std::to_string(id.v)), TVec3f(0, 0, 0), kcl, pa, &error);
                if (!entry->actor) {
                    Core::reportError(error);
                    return;
                }
            } else {
                entry->actor->appear();
                if (rebuild) {
                    entry->actor->setCollision(kcl, pa);
                }
            }
            const float position[] = {t.pos.x, t.pos.y, t.pos.z}, rotation[] = {t.dirDeg.x, t.dirDeg.y, t.dirDeg.z},
                        scale[] = {t.scale.x, t.scale.y, t.scale.z};
            entry->actor->setBuiltScale(TVec3f(t.scale.x, t.scale.y, t.scale.z));
            EditorScene::applyTransform(entry->actor, model.zoneId(zone), position, rotation, scale);
            if (rebuild) {
                EditorTerrain::injectArchive(archivePath, archive.data(), archive.size());
                const char* error = "";
                LiveActor* visual = EditorTerrain::createModel(archiveName, archiveName, entry->actor->mBaseMtx.toMtxPtr(), &error);
                if (!visual) {
                    Core::reportError(error);
                    EditorTerrain::releaseArchive(archivePath);
                    return;
                }
                entry->actor->setModel(visual);
                if (entry->archive) {
                    EditorTerrain::releaseArchive(entry->archive);
                }
                entry->archive = archivePath;
                EditorTerrain::Part::GravityShape shape;
                shape.kind = asset->gravity && !built.kcl.empty() ? EditorTerrain::Part::GravityShape::Kind::Cube :
                                                                    EditorTerrain::Part::GravityShape::Kind::None;
                shape.center.set((built.minimum.x + built.maximum.x) / 2, (built.minimum.y + built.maximum.y) / 2,
                                 (built.minimum.z + built.maximum.z) / 2);
                shape.half.set(std::max(.5f, (built.maximum.x - built.minimum.x) / 2), std::max(.5f, (built.maximum.y - built.minimum.y) / 2),
                               std::max(.5f, (built.maximum.z - built.minimum.z) / 2));
                shape.activeFaces = 1 << 2;
                shape.range = 3000;
                if (!entry->actor->setGravity(shape)) {
                    Core::reportError("Imported gravity needs a free gravity slot; reload to reclaim retired fields.");
                }
                if (entry->actor->mGravity) {
                    entry->actor->mGravity->mAppeared = true;
                }
                PetariNative::HostAllocationScope host;
                entry->name = model.objectName(id);
                entry->scale = t.scale;
            }
            Core::bindActor(id, entry->actor);
        }
    }  // namespace
    void registerWithEditor() {
        Runtime::registerGeneratedRowProvider({owns, sync, retire, beginScene});
    }
    void placeRequested(EditModel& model) {
        auto asset = I::takePlacement();
        if (!asset) {
            return;
        }
        PetariNative::HostAllocationScope host;
        std::string error;
        const auto id = I::storeAsset(*asset, &error);
        if (id.empty()) {
            Core::reportError(error);
            return;
        }
        Vec3 position;
        if (!Runtime::playMarker(&position)) {
            const auto* p = MR::getPlayerPos();
            position = {p->x, p->y, p->z};
        }
        Op op;
        op.label = "Imported " + asset->name;
        std::vector< RowId > rows;
        for (size_t i = 0; i < asset->parts.size(); ++i) {
            AddRow add;
            add.id = newRowId();
            add.zone = 0;
            add.table = "jmp/placement/common/objinfo";
            add.cells.push_back({columnHash("name"), Cell{CellType::StringOffset, 0, 0, I::className(id, i)}});
            auto value = [&](const char* name, float x) { add.cells.push_back({columnHash(name), Cell{CellType::Float, 0, x, {}}}); };
            value("pos_x", position.x);
            value("pos_y", position.y);
            value("pos_z", position.z);
            for (const char* s : {"scale_x", "scale_y", "scale_z"}) {
                value(s, 1);
            }
            rows.push_back(add.id);
            op.adds.push_back(std::move(add));
        }
        if (!model.apply(op, &error)) {
            Core::reportError(error);
            return;
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            PetariNative::EditorPick::selectRow(rows[i].v, i ? PetariNative::EditorPick::Click::Add : PetariNative::EditorPick::Click::Replace);
        }
        Core::reportNotice("Imported " + asset->name + " at the play marker. Drag it with the gizmo; reuse it from the Browser.");
    }
}  // namespace PetariEditor::ImportSync
