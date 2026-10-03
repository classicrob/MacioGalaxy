// The editor runtime's per-frame work (petari/editor/runtime.hpp): the session's EditModel,
// the row <-> actor binding, the command drain and SceneSync (TECH.md 2.1-2.6).
#include "petari/editor/runtime.hpp"

#include "checks.hpp"
#include "edit_model.hpp"
#include "runtime_core.hpp"
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

    struct Session {
        std::unique_ptr< EditModel > model;
        std::uint32_t boundScene = 0;
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
        std::uint64_t checkedRevision = 0;
        std::set< std::string > baselineChecks;  // the problems the galaxy has as shipped: not reported
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

    // The disc itself, without the mod overlay: the EditModel's originals.
    std::optional< Formats::Bytes > readUnmoddedDisc(const std::string& path) {
        const std::filesystem::path files = PetariNative::Platform::DVD::mountInfo().filesDirectory;
        std::ifstream in(files / path.substr(1), std::ios::binary);
        if (files.empty() || !in) {
            return std::nullopt;
        }
        return Formats::Bytes(std::istreambuf_iterator< char >(in), {});
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
        static bool pathChecks = false;
        if (!pathChecks) {
            registerCheckProvider(+[](const ModelView& model, std::vector< Check >& out) {
                auto checks = Paths::computeChecks(model);
                out.insert(out.end(), checks.begin(), checks.end());
            });
            pathChecks = true;
        }
        std::string error;
        Hooks::clearArchiveOverrides();  // another galaxy or mission: the old edits don't apply
        std::string catalogError;
        if (!Catalog::loadWithUser(PetariNative::Platform::ModFolder::userDirectory().string(), &catalogError)) {
            Core::reportError("Catalog: " + catalogError);
        }
        TerrainSync::registerWithEditor();
        s.checkedRevision = 0;
        s.suspectNotices.clear();
        // Planet classes: the names PlanetMapDataTable lists on this disc.
        if (std::optional< Formats::Bytes > raw = readDisc("/ObjectData/PlanetMapDataTable.arc")) {
            if (std::optional< Formats::Archive > archive = Formats::Archive::parse(*raw)) {
                for (const std::string& file : archive->files()) {
                    std::optional< Formats::Table > table = Formats::Table::parse(*archive->file(file));
                    const int column = table ? table->column(Formats::jmapHash("PlanetName")) : -1;
                    std::vector< std::string > names;
                    for (int r = 0; column >= 0 && r < table->rowCount(); r++) {
                        names.push_back(table->get(r, column).s);
                    }
                    Catalog::addPlanets(names);
                }
            }
        }
        s.model = EditModel::load(pStage, scenario, readDisc, &error, readUnmoddedDisc);
        s.actors.clear();
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
        s.actors.clear();
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
        const bool live = endsWith(path, "/objinfo") || endsWith(path, "/mappartsinfo");

        if (!live) {
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
        retireRow(id);
        if (pName == nullptr || pName[0] == '\0') {
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
        } else if (!s.pending.empty()) {
            startReload(from == PlayFrom::Marker ? Session::After::PlayMarker : Session::After::PlayStart);
        } else {
            // Nothing needs a reload: unfreeze where we are (and put Mario on the marker).
            if (Core::uiCallbacks().leaveEdit != nullptr) {
                Core::uiCallbacks().leaveEdit();
            }
            s.teleportPending = from == PlayFrom::Marker && s.hasMarker;
            Core::setMode(Mode::Playing);
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
                         {"Gravity fields", EditorScene::gravityCount(), 128},
                         {"Archives", EditorScene::mountedArchiveCount(), EditorScene::mountedArchiveLimit()}};
    }
}  // namespace

void syncTestTick();    // sync_test.cpp: the SceneSync live check (PETARI_EDITOR_SYNC_TEST)
void reloadTestTick();  // reload_test.cpp: the reload and Play live check (PETARI_EDITOR_RELOAD_TEST)
void saveTestTick();    // save_test.cpp: the save-as-mod live check (PETARI_EDITOR_SAVE_TEST)
void brushTestTick();   // brush_test.cpp: the terrain brush live check (PETARI_EDITOR_BRUSH_TEST)
void redrawTestTick();  // redraw_test.cpp: a re-create draws once (PETARI_EDITOR_REDRAW_TEST=<name>)
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
        return;
    }
    syncTestTick();
    reloadTestTick();
    saveTestTick();
    brushTestTick();
    redrawTestTick();
    pathsTestTick();
    Session& s = session();
    const char* pStage = EditorScene::stageName();
    // (Not while reloading: a mission switch sets the model's scenario before the scene has it.)
    if (mode() != Mode::Off && mode() != Mode::Reloading && pStage != nullptr) {
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
    if (s.model->revision() != s.checkedRevision) {
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
