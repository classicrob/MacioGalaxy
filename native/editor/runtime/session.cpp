// The editor runtime's per-frame work (petari/editor/runtime.hpp): the session's EditModel,
// the row <-> actor binding, the command drain and SceneSync (TECH.md 2.1-2.6).
#include "petari/editor/runtime.hpp"

#include "edit_model.hpp"
#include "runtime_core.hpp"

#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include <petari/editor/runtime_hooks.hpp>
#include <petari/platform/dvd.hpp>
#include <revolution/dvd.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>

namespace PetariEditor::Runtime {
namespace {
    /// A row created live: its one-row table, kept as long as the scene (actors keep iterators).
    struct LiveRow {
        Formats::Bytes bytes;
        JMapInfo info;
    };

    struct Session {
        std::unique_ptr< EditModel > model;
        std::uint32_t boundScene = 0;
        std::map< RowId, std::vector< LiveActor* > > actors;  // every actor a row made, first first
        std::set< RowId > pending;                             // edits that need a reload
        std::vector< std::unique_ptr< LiveRow > > liveRows;
        std::set< RowId > previewing;
        Vec3 playMarker;
    };

    Session& session() {
        static Session sSession;
        return sSession;
    }

    std::optional< Formats::Bytes > readDisc(const std::string& path) {
        // Through the mounted disc, so enabled mod-folder files win as they do for the game.
        const s32 entry = DVDConvertPathToEntrynum(path.c_str());
        if (entry < 0) {
            return std::nullopt;
        }
        std::ifstream in(PetariNative::Platform::DVD::hostPathForEntry(entry), std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
    }

    bool endsWith(const std::string& text, const char* pSuffix) {
        const std::string suffix(pSuffix);
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void loadModel(const char* pStage, int scenario) {
        Session& s = session();
        std::string error;
        s.model = EditModel::load(pStage, scenario, readDisc, &error);
        s.actors.clear();
        s.pending.clear();
        s.boundScene = 0;
        Core::clearBindings();
        Core::setModel(s.model.get());
        if (!s.model) {
            Core::reportError(std::string("Can't edit ") + pStage + ": " + error);
        } else {
            std::fprintf(stderr, "[editor] editing %s mission %d: %d zones\n", pStage, scenario, s.model->zoneCount());
        }
    }

    // After every scene build: the actors placement made, by row (Hooks::placed records them).
    void bindPlacements() {
        Session& s = session();
        s.actors.clear();
        s.liveRows.clear();
        s.previewing.clear();
        Core::clearBindings();
        std::map< const void*, std::pair< int, int > > tableOf;
        for (int zone = 0; zone < s.model->zoneCount(); zone++) {
            for (int table = 0; table < s.model->tableCount(zone); table++) {
                if (const void* pData = EditorScene::tableData(s.model->zoneId(zone), s.model->tablePath(zone, table))) {
                    tableOf[pData] = {zone, table};
                }
            }
        }
        int bound = 0;
        for (const Hooks::Placed& placed : Hooks::placed()) {
            auto it = tableOf.find(placed.table);
            if (it == tableOf.end()) {
                continue;
            }
            const RowId id = s.model->rowAt(it->second.first, it->second.second, placed.row);
            if (id.valid()) {
                s.actors[id].push_back(static_cast< LiveActor* >(placed.actor));
            }
        }
        for (auto& [id, list] : s.actors) {
            Core::bindActor(id, list.front());
            bound++;
        }
        s.boundScene = Hooks::sceneSerial();
        std::fprintf(stderr, "[editor] bound %d rows to their actors\n", bound);
        if (Core::uiCallbacks().sceneRebuilt != nullptr) {
            Core::uiCallbacks().sceneRebuilt();
        }
    }

    void retireRow(RowId id) {
        Session& s = session();
        auto it = s.actors.find(id);
        if (it != s.actors.end()) {
            for (LiveActor* pActor : it->second) {
                EditorScene::retire(pActor);
            }
            s.actors.erase(it);
        }
        Core::unbindRow(id);
    }

    // Re-create a changed row's actors live (objects and map parts), or leave it for a reload.
    void syncRow(RowId id) {
        Session& s = session();
        EditModel& m = *s.model;
        int zone = 0, table = 0;
        if (!m.locate(id, &zone, &table)) {
            return;
        }
        const std::string path = m.tablePath(zone, table);
        const int layer = m.tableLayer(zone, table);
        const bool live = endsWith(path, "/objinfo") || endsWith(path, "/mappartsinfo");
        if (!live) {
            s.pending.insert(id);
            return;
        }
        if (!m.zonePlaced(zone) || layer < 0 || !(m.activeLayers(zone, m.scenario()) & (1u << layer))) {
            return;  // not in this mission's scene; nothing to show
        }
        retireRow(id);
        const char* pName = m.objectName(id);
        if (pName == nullptr || pName[0] == '\0') {
            return;
        }
        int rowIndex = 0;
        const Formats::Table* pTable = m.rowTable(id, &rowIndex);
        auto row = std::make_unique< LiveRow >();
        row->bytes = pTable->buildSingleRow(rowIndex);
        row->info.attach(row->bytes.data());
        row->info.setName("ObjInfo");
        Hooks::registerRows(row->bytes.data(), row->bytes.size(), m.zoneId(zone));
        EditorScene::Created created;
        const char* pError = "";
        if (!EditorScene::createFromRow(JMapInfoIter(&row->info, 0), &created, &pError)) {
            Hooks::unregisterRows(row->bytes.data());
            Core::reportError(std::string(pName) + ": " + pError);
            s.pending.insert(id);
            return;
        }
        std::vector< LiveActor* >& list = s.actors[id];
        for (s32 i = 0; i < created.mActorCount; i++) {
            list.push_back(EditorScene::actorAt(created.mFirstIndex + i));
        }
        if (!list.empty()) {
            Core::bindActor(id, list.front());
        }
        s.pending.erase(id);
        s.liveRows.push_back(std::move(row));
    }

    void preview(const Command& command) {
        Session& s = session();
        for (size_t i = 0; i < command.rows.size() && i < command.previews.size(); i++) {
            LiveActor* pActor = actorForRow(command.rows[i]);
            int zone = 0, table = 0;
            if (pActor == nullptr || !s.model->locate(command.rows[i], &zone, &table)) {
                continue;
            }
            const RowTransform& t = command.previews[i];
            const f32 pos[3] = {t.pos.x, t.pos.y, t.pos.z}, dir[3] = {t.dirDeg.x, t.dirDeg.y, t.dirDeg.z},
                      scale[3] = {t.scale.x, t.scale.y, t.scale.z};
            EditorScene::applyTransform(pActor, s.model->zoneId(zone), pos, dir, scale);
            s.previewing.insert(command.rows[i]);
        }
    }

    void endPreview(const std::vector< RowId >& rows) {
        Session& s = session();
        for (RowId id : rows) {
            LiveActor* pActor = actorForRow(id);
            int zone = 0, table = 0;
            if (pActor != nullptr && s.previewing.count(id) && s.model->locate(id, &zone, &table)) {
                const RowTransform t = s.model->transform(id);
                const f32 pos[3] = {t.pos.x, t.pos.y, t.pos.z}, dir[3] = {t.dirDeg.x, t.dirDeg.y, t.dirDeg.z},
                          scale[3] = {t.scale.x, t.scale.y, t.scale.z};
                EditorScene::applyTransform(pActor, s.model->zoneId(zone), pos, dir, scale);
            }
            s.previewing.erase(id);
        }
    }

    void drain() {
        Session& s = session();
        for (Command& command : Core::drainCommands()) {
            if (!s.model) {
                Core::reportError("No galaxy is loaded for editing");
                continue;
            }
            std::string error;
            switch (command.kind) {
            case Command::Kind::Apply:
                for (const SetCell& set : command.op.sets) {
                    s.previewing.erase(set.row);  // re-created from the row
                }
                if (!s.model->apply(command.op, &error)) {
                    Core::reportError(command.op.label + ": " + error);
                }
                break;
            case Command::Kind::Undo:
                s.model->undo();
                break;
            case Command::Kind::Redo:
                s.model->redo();
                break;
            case Command::Kind::JumpHistory:
                s.model->jumpTo(command.index);
                break;
            case Command::Kind::Preview:
                preview(command);
                break;
            case Command::Kind::EndPreview:
                endPreview(command.rows);
                break;
            case Command::Kind::SetPlayMarker:
                s.playMarker = command.markerPos;
                break;
            case Command::Kind::Play:
            case Command::Kind::Reload:
            case Command::Kind::SaveMod:
            case Command::Kind::SwitchScenario:
                Core::reportError("Not available yet in this build");  // editor/fast-reload, editor/save-mod
                break;
            }
        }
    }

    void updateStatus() {
        Session& s = session();
        Status& st = Core::mutableStatus();
        st.pendingCount = static_cast< int >(s.pending.size());
        st.dirty = !s.pending.empty();
        st.unsaved = s.model && s.model->unsaved();
        u32 heapFree = 0, heapTotal = 0;
        EditorScene::sceneHeap(&heapFree, &heapTotal);
        st.heapFree = heapFree;
        st.heapTotal = heapTotal;
        st.capacities = {{"Objects", EditorScene::liveActorCount(), EditorScene::liveActorLimit()},
                         {"Gravity fields", EditorScene::gravityCount(), 128}};
    }
}  // namespace

void syncTestTick();  // sync_test.cpp: the SceneSync live check (PETARI_EDITOR_SYNC_TEST)

void tick() {
    if (!Hooks::enabled()) {
        return;
    }
    syncTestTick();
    Session& s = session();
    const char* pStage = EditorScene::stageName();
    if (mode() != Mode::Off && pStage != nullptr) {
        if (!s.model || std::string(s.model->galaxy()) != pStage || s.model->scenario() != EditorScene::scenario()) {
            loadModel(pStage, EditorScene::scenario());
        }
    }
    if (!s.model) {
        Core::drainCommands();
        Core::runUiSeamHook();
        return;
    }
    if (s.boundScene != Hooks::sceneSerial()) {
        bindPlacements();
    }
    drain();
    for (RowId id : s.model->takeChangedRows()) {
        syncRow(id);
    }
    updateStatus();
    Core::runUiSeamHook();
}

}  // namespace PetariEditor::Runtime
