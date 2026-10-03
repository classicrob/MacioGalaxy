// The editor runtime's per-frame work (petari/editor/runtime.hpp): the session's EditModel,
// the row <-> actor binding, the command drain and SceneSync (TECH.md 2.1-2.6).
#include "petari/editor/runtime.hpp"

#include "checks.hpp"
#include "edit_model.hpp"
#include "runtime_core.hpp"
#include "model_io.hpp"
#include "save_mod.hpp"
#include "terrain_sync.hpp"
#include <petari/editor/catalog.hpp>
#include <petari/editor/paths.hpp>

#include "Game/Editor/EditorScene.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include <petari/editor/runtime_hooks.hpp>
#include <petari/host_allocation.hpp>
#include <petari/platform/dvd.hpp>
#include <petari/platform/mod_folder.hpp>
#include <revolution/dvd.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
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

    constexpr s32 kGravityLimit = 128;  // PlanetGravityManager's array
    std::vector< PreviewHandler > gPreviewHandlers;
    std::vector< LiveUpdater > gLiveUpdaters;
    std::vector< RowPreviewHandler > gRowPreviewHandlers;
    std::vector< RowUpdater > gRowUpdaters;
    bool gChecksInvalid = false;
    std::vector< PlayListener > gPlayListeners;

    // A gravity field's row (PlanetObjInfo: the Global*Gravity classes): the manager's 128 slots
    // are spent by every creation, retired fields included, until the next scene build.
    bool isGravityTable(const std::string& path) {
        return path.size() >= 14 && path.compare(path.size() - 14, 14, "/planetobjinfo") == 0;
    }

    void pokeTransform(LiveActor* pActor, int zoneId, const RowTransform& t) {
        for (PreviewHandler handler : gPreviewHandlers) {
            if (handler(pActor, zoneId, t)) {
                return;
            }
        }
        const f32 pos[3] = {t.pos.x, t.pos.y, t.pos.z}, dir[3] = {t.dirDeg.x, t.dirDeg.y, t.dirDeg.z}, scale[3] = {t.scale.x, t.scale.y, t.scale.z};
        EditorScene::applyTransform(pActor, zoneId, pos, dir, scale);
    }

    struct Session {
        std::unique_ptr< EditModel > model;
        std::uint32_t boundScene = 0;
        std::map< const void*, std::map< int, RowId > > placementRows;
        std::map< RowId, std::vector< LiveActor* > > actors;  // every actor a row made, first first
        std::set< RowId > pending;                             // edits that need a reload
        std::vector< std::unique_ptr< LiveRow > > liveRows;
        std::set< RowId > previewing;
        std::vector< std::unique_ptr< LiveRow > > railStorage;
        std::map< RowId, std::pair< int, int > > railOwners;
        std::set< std::string > suspectNotices;
        Vec3 playMarker;
        bool hasMarker = false;
        // A reload in flight: what to do once its scene is playable.
        enum class After : std::uint8_t { None, Edit, PlayMarker, PlayStart } after = After::None;
        std::uint32_t reloadFromScene = 0;
        double reloadStartMs = 0;
        bool teleportPending = false;
        bool plainReload = false;  // reloadScene() without an edit session
        bool keepModel = false;    // ensureModel(): the model stays loaded outside Edit
        bool inPlay = false;       // a play-test run is on (Begin/Resume told, ToEdit not yet)
        bool hadRun = false;       // a run began this session (Resume continues it; with none, Resume is a Begin)
        bool scenePlayed = false;  // this scene has been played since it was built (a fresh Play rebuilds it)
        std::uint64_t checkedRevision = 0;
        std::set< std::string > baselineChecks;  // the problems the galaxy has as shipped: not reported
    };

    Session& session() {
        static Session sSession;
        return sSession;
    }

    void firePlay(PlayEvent event) {
        Session& s = session();
        const std::string galaxy = s.model ? s.model->galaxy() : "";
        const int mission = s.model ? s.model->scenario() : 0;
        for (PlayListener listener : gPlayListeners) {
            listener(event, galaxy.c_str(), mission);
        }
        s.inPlay = event == PlayEvent::Begin || event == PlayEvent::Resume;
        s.hadRun = event == PlayEvent::End ? false : (s.hadRun || event == PlayEvent::Begin);
        s.scenePlayed = s.scenePlayed || s.inPlay;
    }

    bool endsWith(const std::string& text, const char* pSuffix) {
        const std::string suffix(pSuffix);
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    double nowMs() {
        return std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void loadModel(const char* pStage, int scenario) {
        PetariNative::HostAllocationScope host;  // editor data, never the game heap
        Session& s = session();
        if (s.model) {
            firePlay(PlayEvent::End);  // another galaxy or mission: this session ends
            if (mode() == Mode::Playing) {
                Core::setMode(Mode::Off);  // ordinary play from here, not a play-test
            }
        }
        std::string error;
        Hooks::clearArchiveOverrides();  // another galaxy or mission: the old edits don't apply
        std::string catalogError;
        s.checkedRevision = 0;
        // Shared with the headless tools (model_io.hpp): the catalog, terrain's brush class, the
        // disc's planets, then the model.
        s.model = ModelIO::load(pStage, scenario, PetariNative::Platform::ModFolder::userDirectory().string(), TerrainSync::registerWithEditor,
                                &error, &catalogError);
        if (!catalogError.empty()) {
            Core::reportError("Catalog: " + catalogError);
        }
        s.suspectNotices.clear();
        s.actors.clear();
        s.placementRows.clear();
        s.pending.clear();
        s.boundScene = 0;
        Core::clearBindings();
        Core::setModel(s.model.get());
        if (!s.model) {
            Core::reportError(std::string("Can't edit ") + pStage + ": " + error);
        } else {
            // The missions' names as the pause menu shows them (ScenarioData's own are internal, Japanese).
            std::vector< std::string > names;
            std::string list;
            for (int mission = 1; mission <= s.model->missionCount(); mission++) {
                char name[256];
                names.push_back(EditorScene::scenarioName(pStage, mission, name, sizeof(name)) ? name : "");
                list += (mission > 1 ? ", " : "") + std::to_string(mission) + " \"" + names.back() + "\"";
            }
            s.model->setMissionNames(std::move(names));
            // The mod the galaxy's archives came from (the Save sheet defaults to it).
            std::string loadedMod;
            for (int zone = 0; zone < s.model->zoneCount() && loadedMod.empty(); zone++) {
                const std::string path = std::string("/StageData/") + s.model->zoneName(zone) + ".arc";
                loadedMod = PetariNative::Platform::DVD::overlayModForEntry(DVDConvertPathToEntrynum(path.c_str()));
            }
            Core::mutableStatus().loadedMod = loadedMod;
            std::fprintf(stderr, "[editor] editing %s mission %d: %d zones; missions %s\n", pStage, scenario, s.model->zoneCount(),
                         list.c_str());
        }
    }

    const JMapInfo* railTable(const Formats::Table* table, int zone) {
        Session& s = session();
        auto row = std::make_unique< LiveRow >();
        if (table) {
            row->bytes = table->build();
        } else {
            row->bytes.assign(16, 0);
            row->bytes[11] = 16;
        }
        row->info.attach(row->bytes.data());
        Hooks::registerRows(row->bytes.data(), row->bytes.size(), s.model->zoneId(zone));
        const JMapInfo* info = &row->info;
        s.railStorage.push_back(std::move(row));
        return info;
    }

    void rebuildRails(int zone) {
        Session& s = session();
        EditModel& m = *s.model;
        for (auto it = s.railOwners.begin(); it != s.railOwners.end();) {
            if (it->second.first == zone) {
                it = s.railOwners.erase(it);
            } else {
                ++it;
            }
        }
        Hooks::RailTables tables;
        const Formats::Table* info = nullptr;
        for (int table = 0; table < m.tableCount(zone); ++table) {
            if (std::string(m.tablePath(zone, table)) != "jmp/path/commonpathinfo") {
                continue;
            }
            m.forEachRow(zone, table, [&](RowId rail) {
                if (m.state(rail) == RowState::Deleted) {
                    return;
                }
                int index = 0;
                info = m.rowTable(rail, &index);
                const auto points = m.railPoints(rail);
                const Formats::Table* pointTable = points.empty() ? nullptr : m.rowTable(points.front(), &index);
                // Even an empty rail occupies a slot: StageDataHolder indexes by the info row ordinal.
                tables.points.push_back(railTable(pointTable, zone));
                const Cell* id = m.cell(rail, columnHash("l_id"));
                s.railOwners[rail] = {zone, id ? id->i : -1};
                for (RowId point : points) {
                    s.railOwners[point] = s.railOwners[rail];
                }
            });
        }
        assert(!info || tables.points.size() == static_cast< std::size_t >(info->rowCount()));
        tables.info = railTable(info, zone);
        Hooks::setRailTables(m.zoneId(zone), std::move(tables));
    }

    // Rail rows are data, not actors. Refresh lookup first, then re-create every affected rider.
    void expandRailChanges(std::set< RowId >& changed) {
        Session& s = session();
        EditModel& m = *s.model;
        std::set< std::pair< int, int > > affected;
        std::set< int > zones;
        const auto rails = Paths::collect(m);
        for (auto it = changed.begin(); it != changed.end();) {
            bool railRow = false;
            auto old = s.railOwners.find(*it);
            if (old != s.railOwners.end()) {
                affected.insert(old->second);
                zones.insert(old->second.first);
                railRow = true;
            }
            int z = -1, t = -1;
            if (m.locate(*it, &z, &t) && std::string(m.tablePath(z, t)).rfind("jmp/path/", 0) == 0) {
                railRow = true;
                zones.insert(z);
                for (const auto& rail : rails) {
                    if (rail.zone == z) {
                        bool match = rail.row == *it;
                        for (const auto& p : rail.points) {
                            match = match || p.row == *it;
                        }
                        if (match) {
                            affected.insert({z, rail.id});
                        }
                    }
                }
            }
            if (railRow) {
                s.pending.erase(*it);
                it = changed.erase(it);
            } else {
                ++it;
            }
        }
        for (int z : zones) {
            rebuildRails(z);
        }
        for (int z : zones) {
            for (int t = 0; t < m.tableCount(z); ++t) {
                const std::string path = m.tablePath(z, t);
                if (path.rfind("jmp/placement/", 0) != 0 && path.rfind("jmp/mapparts/", 0) != 0 && path.rfind("jmp/childobj/", 0) != 0) {
                    continue;
                }
                m.forEachRow(z, t, [&](RowId row) {
                    if (m.state(row) == RowState::Deleted) {
                        return;
                    }
                    const Cell* id = m.cell(row, columnHash("CommonPath_ID"));
                    if (id && affected.count({z, id->i})) {
                        changed.insert(row);
                    }
                });
            }
        }
    }

    // After every scene build: the actors placement made, by row (Hooks::placed records them).
    void bindPlacements() {
        PetariNative::HostAllocationScope host;
        Session& s = session();
        s.scenePlayed = false;
        s.actors.clear();
        s.placementRows.clear();
        s.liveRows.clear();
        Hooks::clearRailTables();
        s.railStorage.clear();
        s.railOwners.clear();
        // Remember original identities for deletes; publish overrides only after a zone's first rail edit.
        for (const auto& rail : Paths::collect(*s.model)) {
            s.railOwners[rail.row] = {rail.zone, rail.id};
            for (const auto& point : rail.points) {
                s.railOwners[point.row] = {rail.zone, rail.id};
            }
        }
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
        for (const Hooks::Placed& placed : Hooks::areas()) {
            const auto table = tableOf.find(placed.table);
            if (table == tableOf.end()) {
                continue;
            }
            const RowId row = s.model->rowAt(table->second.first, table->second.second, placed.row);
            if (row.valid()) {
                s.placementRows[placed.table][placed.row] = row;
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
        TerrainSync::sceneBound(*s.model);  // brush rows: placement made nothing for them
        std::fprintf(stderr, "[editor] bound %d rows to their actors\n", bound);
        if (Core::uiCallbacks().sceneRebuilt != nullptr) {
            Core::uiCallbacks().sceneRebuilt();
        }
    }

    void retireRow(RowId id) {
        TerrainSync::retireBrush(id);  // nothing unless the row was a brush
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

    // Points of the zone's path with this l_id (CommonPathInfo row n -> CommonPathPointInfo.n); -1: no such path.
    int railPointCount(const EditModel& m, int zone, int lid) {
        int railTable = -1;
        for (int t = 0; t < m.tableCount(zone); t++) {
            if (endsWith(m.tablePath(zone, t), "/commonpathinfo")) railTable = t;
        }
        if (railTable < 0 || lid < 0) return -1;
        int index = 0, found = -1;
        m.forEachRow(zone, railTable, [&](RowId row) {
            if (m.state(row) == RowState::Deleted) return;
            const Cell* pLid = m.cell(row, columnHash("l_id"));
            if (found < 0 && pLid != nullptr && pLid->i == lid) found = index;
            index++;
        });
        if (found < 0) return -1;
        const std::string points = "/commonpathpointinfo." + std::to_string(found);
        for (int t = 0; t < m.tableCount(zone); t++) {
            if (endsWith(m.tablePath(zone, t), points.c_str())) {
                int count = 0;
                m.forEachRow(zone, t, [&](RowId row) { count += m.state(row) != RowState::Deleted; });
                return count;
            }
        }
        return 0;
    }

    // Re-create a changed row's actors live (objects and map parts), or leave it for a reload.
    void syncRow(RowId id) {
        Session& s = session();
        EditModel& m = *s.model;
        int zone = 0, table = 0;
        if (!m.locate(id, &zone, &table)) {
            // Undoing an insertion removes the row identity entirely.
            for (RowUpdater updater : gRowUpdaters) {
                if (updater(id, JMapInfoIter())) {
                    break;
                }
            }
            retireRow(id);
            s.pending.erase(id);
            return;
        }
        const std::string path = m.tablePath(zone, table);
        const int layer = m.tableLayer(zone, table);
        if (TerrainSync::isBrush(m.objectName(id))) {
            // Terrain brushes are shown by terrain parts (terrain_sync.cpp), not the factory.
            const bool shown = m.state(id) != RowState::Deleted && m.zonePlaced(zone) && layer >= 0 &&
                               (m.activeLayers(zone, m.scenario()) & (1u << layer));
            if (shown) {
                TerrainSync::syncBrush(m, id);
            } else {
                retireRow(id);
            }
            s.pending.erase(id);
            return;
        }
        const bool gravity = isGravityTable(path);
        const bool live = endsWith(path, "/objinfo") || endsWith(path, "/mappartsinfo") || gravity;

        if (!live) {
            if (!gRowUpdaters.empty()) {
                std::unique_ptr< LiveRow > row;
                if (m.state(id) != RowState::Deleted) {
                    int rowIndex = 0;
                    const auto* tableData = m.rowTable(id, &rowIndex);
                    if (tableData) {
                        PetariNative::HostAllocationScope host;
                        row = std::make_unique< LiveRow >();
                        row->bytes = tableData->buildSingleRow(rowIndex);
                        row->info.attach(row->bytes.data());
                        Hooks::registerRows(row->bytes.data(), row->bytes.size(), m.zoneId(zone));
                    }
                }
                const auto iter = row ? JMapInfoIter(&row->info, 0) : JMapInfoIter();
                for (RowUpdater updater : gRowUpdaters) {
                    if (updater(id, iter)) {
                        s.pending.erase(id);
                        if (row) {
                            s.liveRows.push_back(std::move(row));
                        }
                        return;
                    }
                }
                if (row) {
                    Hooks::unregisterRows(row->bytes.data());
                }
            }
            if (endsWith(path, "/childobjinfo")) {
                Core::reportNotice("Child objects must be rebuilt with their parent; Reload to apply this change");
            }
            s.pending.insert(id);
            return;
        }
        if (m.state(id) == RowState::Deleted) {
            retireRow(id);
            s.pending.erase(id);
            return;
        }
        if (!m.zonePlaced(zone) || layer < 0 || !(m.activeLayers(zone, m.scenario()) & (1u << layer))) {
            return;  // not in this mission's scene; nothing to show
        }
        const char* pName = m.objectName(id);
        const auto* info = pName ? Catalog::find(pName) : nullptr;
        const Cell* railId = m.cell(id, columnHash("CommonPath_ID"));
        if (info && info->liveRecreate == "unsafe" && railId && railId->i >= 0) {
            s.pending.insert(id);
            Core::reportNotice(std::string(pName) + " follows this path but cannot be re-created live; Reload to apply it");
            return;
        }
        if (info && info->liveRecreate == "suspect" && railId && railId->i >= 0 && s.suspectNotices.insert(pName).second) {
            Core::reportNotice(std::string(pName) + " was re-created; its sweep reported a visual or state issue");
        }
        bool required = false;
        if (info) {
            for (const auto& link : info->links) {
                required = required || (link.kind == Catalog::LinkKind::Rail && link.required);
            }
        }
        bool validPath = false;
        if (railId && railId->i >= 0) {
            for (const auto& rail : Paths::collect(m)) {
                validPath = validPath || (rail.zone == zone && rail.id == railId->i && rail.points.size() >= 2);
            }
        }
        if ((required || (railId && railId->i >= 0)) && !validPath) {
            retireRow(id);
            s.pending.insert(id);
            Core::reportNotice(std::string(pName ? pName : "Object") + " needs a valid path before it can be re-created");
            return;
        }
        if (pName == nullptr || pName[0] == '\0') {
            retireRow(id);
            return;
        }
        int rowIndex = 0;
        const Formats::Table* pTable = m.rowTable(id, &rowIndex);
        std::unique_ptr< LiveRow > row;
        {
            PetariNative::HostAllocationScope host;  // the row is editor data; the actor (below) is the game's
            row = std::make_unique< LiveRow >();
            row->bytes = pTable->buildSingleRow(rowIndex);
            row->info.attach(row->bytes.data());
            row->info.setName("ObjInfo");
            Hooks::registerRows(row->bytes.data(), row->bytes.size(), m.zoneId(zone));
        }
        // Wire gravity divides by its point count (Obj_arg0 + 2) and walks its path: a bad row would
        // crash or hang the constructor (or an in-place rebuild).
        if (std::string(pName) == "GlobalWireGravity") {
            const Cell* pPoints = m.cell(id, columnHash("Obj_arg0"));
            const Cell* pPath = m.cell(id, columnHash("CommonPath_ID"));
            if (pPoints == nullptr || pPoints->i < 0 || pPoints->i > 4096 || pPath == nullptr || railPointCount(m, zone, pPath->i) < 2) {
                Hooks::unregisterRows(row->bytes.data());
                s.pending.insert(id);
                Core::reportError("Wire gravity needs a path of 2 or more points and 0-4096 sample points (Obj_arg0)");
                return;
            }
        }
        // In place when a registered updater can (no new actor; for gravity, no new slot).
        if (LiveActor* pCurrent = actorForRow(id)) {
            for (LiveUpdater updater : gLiveUpdaters) {
                if (updater(id, pCurrent, JMapInfoIter(&row->info, 0))) {
                    s.pending.erase(id);
                    s.liveRows.push_back(std::move(row));
                    return;
                }
            }
        }
        // A new gravity field needs a free slot; at the cap the current one stays until a Reload.
        if (gravity && EditorScene::gravityCount() >= kGravityLimit) {
            Hooks::unregisterRows(row->bytes.data());
            s.pending.insert(id);
            Core::reportNotice("All 128 gravity fields are in use (retired ones count until a Reload): Reload to apply this change");
            return;
        }
        retireRow(id);
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

    bool previewRow(RowId row, int zone, const RowTransform& transform) {
        for (RowPreviewHandler handler : gRowPreviewHandlers) {
            if (handler(row, zone, transform)) {
                return true;
            }
        }
        if (auto* actor = actorForRow(row)) {
            pokeTransform(actor, zone, transform);
            return true;
        }
        return false;
    }
    void preview(const Command& command) {
        Session& s = session();
        for (size_t i = 0; i < command.rows.size() && i < command.previews.size(); i++) {
            int zone = 0, table = 0;
            if (s.model->locate(command.rows[i], &zone, &table) && previewRow(command.rows[i], s.model->zoneId(zone), command.previews[i])) {
                s.previewing.insert(command.rows[i]);
            }
        }
    }

    void endPreview(const std::vector< RowId >& rows) {
        Session& s = session();
        for (RowId id : rows) {
            int zone = 0, table = 0;
            if (s.previewing.count(id) && s.model->locate(id, &zone, &table)) {
                previewRow(id, s.model->zoneId(zone), s.model->transform(id));
            }
            s.previewing.erase(id);
        }
    }

    // Rebuild the scene from the model (TECH.md 2.6): the edited archives replace the cached ones.
    void startReload(Session::After after, bool fromStart = false, int scenario = -1) {
        PetariNative::HostAllocationScope host;
        Session& s = session();
        for (auto& [path, bytes] : s.model->changedArchives()) {
            Hooks::setArchiveOverride(path, std::move(bytes));
        }
        Hooks::setEditorReload(true);
        s.after = after;
        s.reloadFromScene = Hooks::sceneSerial();
        s.reloadStartMs = nowMs();
        Core::setMode(Mode::Reloading);
        EditorReload::requestReload(fromStart || after == Session::After::PlayStart, scenario);
    }

    void play(PlayFrom from) {
        Session& s = session();
        if (from == PlayFrom::MissionStart) {
            startReload(Session::After::PlayStart);
        } else if (!s.pending.empty() || (from == PlayFrom::Marker && s.scenePlayed)) {
            // Edits to rebuild, or a fresh run in a scene already played (UX 3: collected and
            // defeated objects, switches and Mario's state reset; only Resume keeps them).
            startReload(from == PlayFrom::Marker ? Session::After::PlayMarker : Session::After::PlayStart);
        } else {
            // Nothing needs a reload: unfreeze where we are (and put Mario on the marker).
            if (Core::uiCallbacks().leaveEdit != nullptr) {
                Core::uiCallbacks().leaveEdit();
            }
            s.teleportPending = from == PlayFrom::Marker && s.hasMarker;
            Core::setMode(Mode::Playing);
            firePlay(from == PlayFrom::Resume && s.hadRun ? PlayEvent::Resume : PlayEvent::Begin);
        }
    }

    // A reload's new scene is up and playable.
    void finishReload() {
        Session& s = session();
        Hooks::setEditorReload(false);
        s.pending.clear();
        std::fprintf(stderr, "[editor] reload done in %.0f ms\n", nowMs() - s.reloadStartMs);
        const Session::After after = s.after;
        s.after = Session::After::None;
        if (after == Session::After::Edit) {
            Core::setMode(Mode::Off);
            if (Core::uiCallbacks().resumeEdit != nullptr) {
                Core::uiCallbacks().resumeEdit();  // the UI re-enters Edit and calls notifyEditEntered
            } else {
                notifyEditEntered();
            }
        } else {
            if (Core::uiCallbacks().leaveEdit != nullptr) {
                Core::uiCallbacks().leaveEdit();
            }
            s.teleportPending = after == Session::After::PlayMarker && s.hasMarker;
            Core::setMode(Mode::Playing);
            firePlay(PlayEvent::Begin);  // a rebuilt scene is a new run
        }
    }

    void drain() {
        Session& s = session();
        std::vector< Command > commands;
        {
            PetariNative::HostAllocationScope host;
            commands = Core::drainCommands();
        }
        for (size_t i = 0; i < commands.size(); i++) {
            Command& command = commands[i];
            auto requeueRest = [&]() {
                for (size_t j = i + 1; j < commands.size(); j++) {
                    enqueue(std::move(commands[j]));
                }
            };
            if (!s.model) {
                Core::reportError("No galaxy is loaded for editing");
                continue;
            }
            std::string error;
            switch (command.kind) {
            case Command::Kind::Apply: {
                PetariNative::HostAllocationScope host;
                for (const SetCell& set : command.op.sets) {
                    s.previewing.erase(set.row);  // re-created from the row
                }
                if (!s.model->apply(command.op, &error)) {
                    Core::reportError(command.op.label + ": " + error);
                }
                break;
            }
            case Command::Kind::Undo: {
                PetariNative::HostAllocationScope host;
                s.model->undo();
                break;
            }
            case Command::Kind::Redo: {
                PetariNative::HostAllocationScope host;
                s.model->redo();
                break;
            }
            case Command::Kind::JumpHistory: {
                PetariNative::HostAllocationScope host;
                s.model->jumpTo(command.index);
                break;
            }
            case Command::Kind::Preview:
                preview(command);
                break;
            case Command::Kind::EndPreview:
                endPreview(command.rows);
                break;
            case Command::Kind::SetPlayMarker:
                s.playMarker = command.markerPos;
                s.hasMarker = true;
                break;
            case Command::Kind::Play:
                play(command.from);
                requeueRest();  // the rest waits for the scene Play leads to
                return;
            case Command::Kind::Reload:
                startReload(Session::After::Edit);
                requeueRest();
                return;
            case Command::Kind::SaveMod: {
                PetariNative::HostAllocationScope host;
                std::string notice;
                if (saveMod(*s.model, command.modName, command.fullArchives, &notice, &error)) {
                    s.model->markSaved();
                    Core::reportNotice(notice);
                    std::fprintf(stderr, "[editor] %s\n", notice.c_str());
                } else {
                    Core::reportError("Save: " + error);
                }
                break;
            }
            case Command::Kind::SwitchScenario: {
                if (command.index == s.model->scenario()) {
                    break;
                }
                bool switched;
                {
                    PetariNative::HostAllocationScope host;
                    switched = s.model->setScenario(command.index);
                }
                if (!switched) {
                    Core::reportError("There is no mission " + std::to_string(command.index));
                    break;
                }
                std::fprintf(stderr, "[editor] switching to mission %d\n", command.index);
                startReload(Session::After::Edit, true, command.index);
                requeueRest();
                return;
            }
            }
        }
    }

    void updateStatus() {
        PetariNative::HostAllocationScope host;
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
                         {"Gravity fields", EditorScene::gravityCount(), kGravityLimit},
                         {"Archives", EditorScene::mountedArchiveCount(), EditorScene::mountedArchiveLimit()}};
    }
}  // namespace

void syncTestTick();    // sync_test.cpp: the SceneSync live check (PETARI_EDITOR_SYNC_TEST)
void reloadTestTick();  // reload_test.cpp: the reload and Play live check (PETARI_EDITOR_RELOAD_TEST)
void saveTestTick();    // save_test.cpp: the save-as-mod live check (PETARI_EDITOR_SAVE_TEST)
void brushTestTick();   // brush_test.cpp: the terrain brush live check (PETARI_EDITOR_BRUSH_TEST)
void redrawTestTick();  // redraw_test.cpp: a re-create draws once (PETARI_EDITOR_REDRAW_TEST=<name>)
void areasTestTick();
void arrayTestTick();
void ensureTestTick();  // ensure_test.cpp: ensureModel in Play (PETARI_EDITOR_ENSURE_TEST)
void pathsTestTick();   // paths_test.cpp: rail commit and undo re-create riders without a reload

bool reloadScene(bool fromStart) {
    Session& s = session();
    if (mode() == Mode::Reloading || s.plainReload || EditorScene::stageName() == nullptr) {
        return false;
    }
    if (s.model && mode() == Mode::Editing) {
        // The editor's own Reload (edits included); fromStart: from the mission's start, then Edit.
        startReload(Session::After::Edit, fromStart);
        return true;
    }
    Hooks::setEditorReload(true);
    s.plainReload = true;
    s.reloadFromScene = Hooks::sceneSerial();
    s.reloadStartMs = nowMs();
    EditorReload::requestReload(fromStart);
    return true;
}

bool ensureModel() {
    Session& s = session();
    const char* pStage = EditorScene::stageName();
    if (!Hooks::enabled()) {
        Core::reportError("The level editor is off (Home menu, Mods page; or PETARI_EDITOR=1)");
        return false;
    }
    if (pStage == nullptr || mode() == Mode::Reloading) {
        return false;
    }
    s.keepModel = true;
    if (!s.model || std::string(s.model->galaxy()) != pStage || s.model->scenario() != EditorScene::scenario()) {
        PetariNative::HostAllocationScope host;
        loadModel(pStage, EditorScene::scenario());
    }
    if (s.model && s.boundScene != Hooks::sceneSerial()) {
        bindPlacements();
    }
    return s.model != nullptr;
}

void registerPlayListener(PlayListener listener) {
    PetariNative::HostAllocationScope host;  // the list outlives scenes
    if (std::find(gPlayListeners.begin(), gPlayListeners.end(), listener) == gPlayListeners.end()) gPlayListeners.push_back(listener);
}

bool requestEdit(const char* reason) {
    if (mode() != Mode::Playing) {
        return false;
    }
    if (reason != nullptr && reason[0] != '\0') {
        Core::reportNotice(reason);
    }
    if (Core::uiCallbacks().resumeEdit != nullptr) {
        Core::uiCallbacks().resumeEdit();  // the UI re-enters Edit (its camera, panels) and calls notifyEditEntered
    } else {
        notifyEditEntered();
    }
    firePlay(PlayEvent::ToEdit);
    return true;
}

bool interceptMiss() {
    Session& s = session();
    if (mode() != Mode::Playing || !s.model || !Hooks::enabled()) {
        return false;
    }
    Core::reportNotice("Missed: back in Edit");
    startReload(Session::After::Edit);  // the scene from the edits, then Edit; no lives lost (editor reloads skip the counter)
    return true;
}

void registerPreviewHandler(PreviewHandler handler) {
    PetariNative::HostAllocationScope host;  // the list outlives scenes
    if (std::find(gPreviewHandlers.begin(), gPreviewHandlers.end(), handler) == gPreviewHandlers.end()) gPreviewHandlers.push_back(handler);
}

void registerLiveUpdater(LiveUpdater updater) {
    PetariNative::HostAllocationScope host;  // the list outlives scenes
    if (std::find(gLiveUpdaters.begin(), gLiveUpdaters.end(), updater) == gLiveUpdaters.end()) gLiveUpdaters.push_back(updater);
}

void registerRowPreviewHandler(RowPreviewHandler handler) {
    if (std::find(gRowPreviewHandlers.begin(), gRowPreviewHandlers.end(), handler) == gRowPreviewHandlers.end()) {
        gRowPreviewHandlers.push_back(handler);
    }
}
void registerRowUpdater(RowUpdater updater) {
    if (std::find(gRowUpdaters.begin(), gRowUpdaters.end(), updater) == gRowUpdaters.end()) {
        gRowUpdaters.push_back(updater);
    }
}
RowId rowForPlacement(const void* data, int index) {
    const auto& tables = session().placementRows;
    const auto table = tables.find(data);
    if (table == tables.end()) {
        return {};
    }
    const auto row = table->second.find(index);
    return row == table->second.end() ? RowId{} : row->second;
}

void invalidateChecks() {
    gChecksInvalid = true;
}

bool playMarker(Vec3* position) {
    if (!session().hasMarker) {
        return false;
    }
    if (position) {
        *position = session().playMarker;
    }
    return true;
}

bool reloading() {
    return mode() == Mode::Reloading || session().plainReload;
}

void plainReloadTestTick(bool playable);  // reload_test.cpp (PETARI_EDITOR_PLAIN_RELOAD_TEST)

void tick(bool playable) {
    plainReloadTestTick(playable);
    // A plain reloadScene() completes whether or not the editor is on.
    if (session().plainReload && Hooks::sceneSerial() != session().reloadFromScene && playable) {
        session().plainReload = false;
        Hooks::setEditorReload(false);
        std::fprintf(stderr, "[editor] scene reload done in %.0f ms\n", nowMs() - session().reloadStartMs);
    }
    if (!Hooks::enabled()) {
        if (session().model) {
            // The editor was turned off: the session ends once.
            PetariNative::HostAllocationScope host;
            firePlay(PlayEvent::End);
            Core::clearBindings();
            Core::setModel(nullptr);
            session().model.reset();
            session().keepModel = false;
        }
        return;
    }
    // Back in Edit after a run (Tab; the UI's leave/enter, or a reload that returns to Edit).
    if (session().inPlay && mode() == Mode::Editing) {
        firePlay(PlayEvent::ToEdit);
    }
    syncTestTick();
    reloadTestTick();
    saveTestTick();
    brushTestTick();
    redrawTestTick();
    ensureTestTick();
    pathsTestTick();
    areasTestTick();
    arrayTestTick();
    Session& s = session();
    const char* pStage = EditorScene::stageName();
    // (Not while reloading: a mission switch sets the model's scenario before the scene has it.)
    // Terrain brushes are shown by the editor, so a scene with brush rows loads the model outside
    // Edit too (Hooks::noteBrushRow, from placement).
    if ((mode() != Mode::Off || s.keepModel || Hooks::sceneHasBrushRows()) && mode() != Mode::Reloading && pStage != nullptr) {
        PetariNative::HostAllocationScope host;
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
    if (mode() == Mode::Reloading) {
        if (Hooks::sceneSerial() != s.reloadFromScene && playable) {
            finishReload();
        } else {
            updateStatus();
            Core::runUiSeamHook();
            return;  // commands wait for the new scene
        }
    }
    if (s.teleportPending && playable) {
        EditorReload::teleportPlayer(s.playMarker.x, s.playMarker.y, s.playMarker.z);
        s.teleportPending = false;
    }
    drain();
    std::set< RowId > changed;
    {
        PetariNative::HostAllocationScope host;
        changed = s.model->takeChangedRows();
        expandRailChanges(changed);
    }
    for (RowId id : changed) {
        syncRow(id);
    }
    if (!changed.empty()) {
        EditorScene::flushConnectionRequests();  // Edit mode is frozen: no movement step to do it
    }
    if (s.model->revision() != s.checkedRevision || (gChecksInvalid && s.checkedRevision != 0)) {
        gChecksInvalid = false;
        PetariNative::HostAllocationScope host;
        std::vector< Check > all = computeChecks(*s.model);
        if (s.checkedRevision == 0) {
            // The first pass is the galaxy as loaded: quirks of the shipped data (a class the game
            // ignores, a switch only one object uses) are its own, not the user's.
            s.baselineChecks.clear();
            for (const Check& c : all) {
                s.baselineChecks.insert(c.key);
            }
        }
        std::vector< Check >& shown = Core::mutableChecks();
        shown.clear();
        for (Check& c : all) {
            if (!s.baselineChecks.count(c.key)) {
                shown.push_back(std::move(c));
            }
        }
        s.checkedRevision = s.model->revision();
    }
    updateStatus();
    Core::runUiSeamHook();
}

}  // namespace PetariEditor::Runtime
