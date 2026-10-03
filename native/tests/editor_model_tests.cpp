// Tests for the level editor's EditModel (native/editor/runtime/edit_model.hpp) on a synthetic
// two-zone galaxy built with the editor's own writers: loading, zone placement and layers, cell
// reads, ops, undo/redo/jump, revert, added columns, changed archives and the saved state; and,
// with an extracted disc, Good Egg itself. No game code.

#include "checks.hpp"
#include "edit_model.hpp"
#include <petari/editor/catalog.hpp>
#include <petari/editor/model_ops.hpp>
#include <petari/editor/patch.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <unistd.h>

namespace E = PetariEditor;
namespace F = PetariEditor::Formats;
namespace fs = std::filesystem;

namespace {
int checks = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
        std::exit(1);
    }
}

F::CellValue i32(int v) { return F::CellValue{F::CellValue::Kind::Int, v, 0, {}}; }
F::CellValue f32(float v) { return F::CellValue{F::CellValue::Kind::Float, 0, v, {}}; }
F::CellValue str(const char* v) { return F::CellValue{F::CellValue::Kind::String, 0, 0, v}; }

struct Col {
    const char* name;
    F::ColumnType type;
};
F::Bytes table(const std::vector< Col >& cols, const std::vector< std::vector< F::CellValue > >& rows) {
    F::Bytes empty(16, 0);
    empty[11] = 16;  // dataOffset = 16, no fields, no rows
    F::Table t = *F::Table::parse(empty);
    for (const Col& c : cols) t.addColumn(F::jmapHash(c.name), c.type, c.type == F::ColumnType::StringOffset ? str("") : i32(0));
    for (const auto& row : rows) {
        const int r = t.addRow();
        for (size_t c = 0; c < row.size(); c++) t.set(r, static_cast< int >(c), row[c]);
    }
    return t.build();
}

const std::vector< Col > kObj = {{"name", F::ColumnType::StringOffset}, {"l_id", F::ColumnType::Long}, {"pos_x", F::ColumnType::Float},
                                 {"pos_y", F::ColumnType::Float},      {"pos_z", F::ColumnType::Float}, {"Obj_arg0", F::ColumnType::Long}};

std::map< std::string, F::Bytes > syntheticDisc() {
    std::map< std::string, F::Bytes > disc;
    F::Bytes scenario = table({{"ScenarioNo", F::ColumnType::Long}, {"TestGalaxy", F::ColumnType::Long}, {"TestZone", F::ColumnType::Long}},
                              {{i32(1), i32(0), i32(1)}, {i32(2), i32(0), i32(2)}});
    F::Bytes zones = table({{"ZoneName", F::ColumnType::StringOffset}}, {{str("TestGalaxy")}, {str("TestZone")}});
    disc["/StageData/TestGalaxy/TestGalaxyScenario.arc"] =
        F::yaz0Encode(F::Archive::create("stage", {{"ScenarioData.bcsv", scenario}, {"ZoneList.bcsv", zones}}).build());
    F::Bytes stageObj = table({{"name", F::ColumnType::StringOffset}, {"l_id", F::ColumnType::Long}, {"pos_x", F::ColumnType::Float},
                               {"pos_y", F::ColumnType::Float}, {"pos_z", F::ColumnType::Float}, {"dir_x", F::ColumnType::Float},
                               {"dir_y", F::ColumnType::Float}, {"dir_z", F::ColumnType::Float}},
                              {{str("TestZone"), i32(0), f32(100), f32(0), f32(0), f32(0), f32(90), f32(0)}});
    F::Bytes galaxyObj = table(kObj, {{str("Coin"), i32(1), f32(1), f32(2), f32(3), i32(-1)}});
    disc["/StageData/TestGalaxy.arc"] = F::Archive::create("stage", {{"jmp/Placement/Common/StageObjInfo", stageObj},
                                                                     {"jmp/Placement/Common/ObjInfo", galaxyObj}})
                                            .build();
    F::Bytes zoneObj = table(kObj, {{str("Kuribo"), i32(5), f32(10), f32(20), f32(30), i32(2)},
                                    {str("Coin"), i32(6), f32(0), f32(0), f32(0), i32(-1)}});
    F::Bytes zoneObjB = table(kObj, {{str("Takobo"), i32(7), f32(0), f32(0), f32(0), i32(-1)}});
    disc["/StageData/TestZone.arc"] =
        F::Archive::create("stage", {{"jmp/Placement/LayerA/ObjInfo", zoneObj}, {"jmp/Placement/LayerB/ObjInfo", zoneObjB}}).build();
    return disc;
}

E::RowId findRow(const E::EditModel& m, int zone, const char* table, const char* name) {
    for (int t = 0; t < m.tableCount(zone); t++) {
        if (std::string(m.tablePath(zone, t)) != table) continue;
        E::RowId found;
        m.forEachRow(zone, t, [&](E::RowId id) {
            if (!found.valid() && std::string(m.objectName(id)) == name) found = id;
        });
        return found;
    }
    return {};
}

E::SetCell setFloat(E::RowId row, const char* column, float v) {
    return E::SetCell{row, E::columnHash(column), E::Cell{E::CellType::Float, 0, v, {}}};
}

void testSynthetic() {
    std::map< std::string, F::Bytes > disc = syntheticDisc();
    E::FileSource source = [&](const std::string& path) -> std::optional< F::Bytes > {
        auto it = disc.find(path);
        return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
    };
    std::string error;
    check(!E::EditModel::load("Missing", 1, source, &error) && !error.empty(), "missing galaxy refused");
    std::unique_ptr< E::EditModel > m = E::EditModel::load("TestGalaxy", 1, source, &error);
    check(m != nullptr, "loads: " + error);
    check(m->zoneCount() == 2 && std::string(m->zoneName(1)) == "TestZone" && m->zoneId(1) == 1, "zones in ZoneList order");
    check(m->zonePlaced(0) && m->zonePlaced(1), "zone placed by StageObjInfo");
    const E::Mtx34 w = m->zoneWorldMatrix(1);
    check(w.m[0][3] == 100.0f && std::fabs(w.m[0][2] - 1.0f) < 1e-5f && std::fabs(w.m[2][0] + 1.0f) < 1e-5f, "zone matrix: T(100) Ry(90)");
    check(m->activeLayers(1, 1) == 0b011 && m->activeLayers(1, 2) == 0b101 && m->activeLayers(0, 1) == 1, "scenario layer masks");

    const E::RowId kuribo = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
    check(kuribo.valid() && std::string(m->objectName(kuribo)) == "Kuribo", "row found by name");
    check(m->columns(1, 0).size() == 6 && std::string(m->columns(1, 0)[2].name) == "pos_x", "columns named");
    check(m->transform(kuribo).pos.y == 20.0f && m->transform(kuribo).scale.x == 1.0f, "transform with default scale");
    check(m->state(kuribo) == E::RowState::Original && m->changedArchives().empty() && !m->unsaved(), "pristine");
    // A string edit undone leaves the table as loaded (its string stays in the pool): no changed archive.
    {
        const E::RowId any = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
        check(m->apply(E::Op{"rename", {E::SetCell{any, E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, "SomethingLong"}}}, {}},
                       &error),
              error);
        m->undo();
        check(m->changedArchives().empty(), "an undone rename leaves no changed archive");
    }

    E::Op move{"Move Goomba", {setFloat(kuribo, "pos_y", 25.0f)}, {}};
    check(m->apply(move, &error), "apply");
    check(m->cell(kuribo, E::columnHash("pos_y"))->f == 25.0f && m->originalCell(kuribo, E::columnHash("pos_y"))->f == 20.0f,
          "current and original");
    check(m->state(kuribo) == E::RowState::Changed && m->unsaved() && m->historySize() == 1 && m->historyCursor() == 1, "changed");
    check(m->takeChangedRows() == std::set< E::RowId >{kuribo} && m->takeChangedRows().empty(), "changed rows for SceneSync");
    auto archives = m->changedArchives();
    check(archives.size() == 1 && archives[0].first == "/StageData/TestZone.arc", "only the zone archive changes");
    std::optional< F::Archive > saved = F::Archive::parse(archives[0].second);
    F::Table savedTable = *F::Table::parse(*saved->file("jmp/placement/layera/objinfo"));
    check(savedTable.get(0, savedTable.column(F::jmapHash("pos_y"))).f == 25.0f, "the saved archive has the edit");
    check(*saved->file("jmp/placement/layerb/objinfo") == *F::Archive::parse(disc["/StageData/TestZone.arc"])->file("jmp/placement/layerb/objinfo"),
          "untouched tables byte for byte");

    m->markSaved();
    check(!m->unsaved(), "saved");
    check(m->undo() && m->state(kuribo) == E::RowState::Original && m->changedArchives().empty() && m->unsaved(), "undo");
    check(m->redo() && m->cell(kuribo, E::columnHash("pos_y"))->f == 25.0f && !m->unsaved(), "redo back to the saved state");

    // A column the table lacks is added; undo leaves it at its neutral value.
    E::Op path{"Set path", {E::SetCell{kuribo, E::columnHash("CommonPath_ID"), E::Cell{E::CellType::Short, 3, 0, {}}}}, {}};
    check(m->apply(path, &error) && m->cell(kuribo, E::columnHash("CommonPath_ID"))->i == 3, "added column");
    check(m->originalCell(kuribo, E::columnHash("CommonPath_ID")) == nullptr, "no original for an added column");
    const E::RowId coin = findRow(*m, 1, "jmp/placement/layera/objinfo", "Coin");
    check(m->cell(coin, E::columnHash("CommonPath_ID"))->i == -1, "other rows get -1");

    // Strings and ints; revert puts every cell back.
    E::Op rename{"Rename", {E::SetCell{coin, E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, "StarPiece"}},
                            E::SetCell{coin, E::columnHash("Obj_arg0"), E::Cell{E::CellType::Int, 4, 0, {}}}},
                 {}};
    check(m->apply(rename, &error) && std::string(m->objectName(coin)) == "StarPiece", "rename");
    E::Op revert{"Revert Coin", {}, {coin}};
    check(m->apply(revert, &error) && std::string(m->objectName(coin)) == "Coin" && m->cell(coin, E::columnHash("Obj_arg0"))->i == -1,
          "revert");
    check(m->historySize() == 4 && std::string(m->historyLabel(3)) == "Revert Coin", "history labels");

    // A new op after undo drops the redo tail.
    check(m->jumpTo(1) && m->historyCursor() == 1 && std::string(m->objectName(coin)) == "Coin", "jump back");
    check(m->apply(E::Op{"Nudge", {setFloat(coin, "pos_x", 9.0f)}, {}}, &error) && m->historySize() == 2, "redo tail dropped");
    check(m->jumpTo(0) && m->state(kuribo) == E::RowState::Original, "jump to the start");

    // Missions: the zone is placed in every mission (a Common StageObjInfo row); its layer A rows
    // appear in mission 1 (mask 1), layer B in mission 2 (mask 2), the galaxy's Common rows in both.
    check(m->missionCount() == 2, "two missions");
    {
        const std::uint64_t before = m->revision();
        check(m->setScenario(2) && m->scenario() == 2 && m->revision() == before + 1, "switch to mission 2");
        check(!m->setScenario(3) && !m->setScenario(0) && m->scenario() == 2, "no mission 3");
        check(m->setScenario(1) && m->scenario() == 1, "back to mission 1");
        m->setMissionNames({"Bunnies in the Wind", ""});
        check(std::string(m->missionName(1)) == "Bunnies in the Wind", "the player-facing mission name");
        m->setMissionNames({});
    }
    const E::RowId takobo = findRow(*m, 1, "jmp/placement/layerb/objinfo", "Takobo");
    const E::RowId galaxyCoin = findRow(*m, 0, "jmp/placement/common/objinfo", "Coin");
    check(m->rowMissions(kuribo) == 0b01 && m->rowMissions(takobo) == 0b10 && m->rowMissions(galaxyCoin) == 0b11, "rows' missions");

    // Moving a zone's StageObjInfo row moves the zone.
    const E::RowId zoneRow = findRow(*m, 0, "jmp/placement/common/stageobjinfo", "TestZone");
    check(m->apply(E::Op{"Move zone", {setFloat(zoneRow, "pos_x", -50.0f)}, {}}, &error) && m->zoneWorldMatrix(1).m[0][3] == -50.0f,
          "zone matrix follows its row");
    check(!m->apply(E::Op{"Bad", {setFloat(E::RowId{12345}, "pos_x", 0)}, {}}, &error) && !error.empty(), "unknown row refused");
    check(m->historySize() == 1, "a refused op records nothing");
    check((E::newRowId().v >> 63) == 1 && E::newRowId() != E::newRowId(), "new row ids are their own, distinct");
    check(m->revision() > 1, "revision bumps");
}

// A mod saved earlier is enabled: the game's view has its edit, the disc doesn't. The edit counts
// as a change (so the next save keeps it), and reverting it goes back to the disc.
// Adding (duplicate, new, into a layer the zone lacks), deleting, undo and redo, failures.
void testStructure() {
    std::map< std::string, F::Bytes > disc = syntheticDisc();
    E::FileSource source = [&](const std::string& path) -> std::optional< F::Bytes > {
        auto it = disc.find(path);
        return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
    };
    std::string error;
    std::unique_ptr< E::EditModel > m = E::EditModel::load("TestGalaxy", 1, source, &error);
    const E::RowId kuribo = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
    const E::RowId coin = findRow(*m, 1, "jmp/placement/layera/objinfo", "Coin");
    m->takeChangedRows();

    // Duplicate: a copy with a fresh l_id; the reserved id names it at once.
    const E::RowId copy = E::newRowId();
    E::AddRow dup;
    dup.id = copy;
    dup.zone = 1;
    dup.table = "jmp/placement/layera/objinfo";
    dup.copyOf = kuribo;
    dup.cells.push_back({E::columnHash("pos_x"), E::Cell{E::CellType::Float, 0, 77.0f, {}}});
    E::Op duplicate{"Duplicate Goomba", {}, {}, {dup}, {}};
    check(m->apply(duplicate, &error), "duplicate: " + error);
    check(m->state(copy) == E::RowState::Added && std::string(m->objectName(copy)) == "Kuribo", "the copy is an added Kuribo");
    check(m->cell(copy, E::columnHash("l_id"))->i == 8 && m->cell(copy, E::columnHash("Obj_arg0"))->i == 2, "fresh l_id, args copied");
    check(m->transform(copy).pos.x == 77.0f && m->originalCell(copy, E::columnHash("pos_x")) == nullptr, "override and no original");
    check(m->rowMissions(copy) == 0b01 && m->takeChangedRows().count(copy) == 1, "missions and SceneSync see it");

    // At a position (a path's points): first in the table; undo takes it out, redo puts it back there.
    E::AddRow first;
    first.id = E::newRowId();
    first.zone = 1;
    first.table = "jmp/placement/layera/objinfo";
    first.copyOf = coin;
    first.at = 0;
    check(m->apply(E::Op{"Insert first", {}, {}, {first}, {}}, &error), "insert at 0: " + error);
    auto firstRow = [&]() {
        E::RowId found;
        for (int t = 0; t < m->tableCount(1); t++)
            if (std::string(m->tablePath(1, t)) == "jmp/placement/layera/objinfo") m->forEachRow(1, t, [&](E::RowId id) { if (!found.valid()) found = id; });
        return found;
    };
    check(firstRow() == first.id, "the inserted row is first");
    m->undo();
    check(firstRow() == kuribo, "undo takes it out");
    m->redo();
    check(firstRow() == first.id, "redo puts it back at its index");
    m->undo();
    // A new row in a layer table the zone lacks (layer C): the table is made from layer A's columns.
    E::AddRow fresh;
    fresh.zone = 1;
    fresh.table = "jmp/placement/layerc/objinfo";
    fresh.cells.push_back({E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, "StarPiece"}});
    check(m->apply(E::Op{"Add Star Bit", {}, {}, {fresh}, {}}, &error), "new layer table: " + error);
    auto archives = m->changedArchives();
    check(archives.size() == 1, "one changed archive");
    std::optional< F::Archive > saved = F::Archive::parse(archives[0].second);
    check(saved && saved->has("jmp/placement/layerc/objinfo") && F::Table::parse(*saved->file("jmp/placement/layerc/objinfo"))->rowCount() == 1,
          "the rebuilt archive has the new layer table");
    check(F::Table::parse(*saved->file("jmp/placement/layera/objinfo"))->rowCount() == 3, "and layer A with the duplicate");

    // Delete: gone from the table, still listed (Deleted) and readable; undo puts it back in place.
    check(m->apply(E::Op{"Delete Coin", {}, {}, {}, {coin}}, &error), "delete");
    check(m->state(coin) == E::RowState::Deleted && std::string(m->objectName(coin)) == "Coin", "a deleted row reads as it was");
    int listed = 0;
    m->forEachRow(1, 0, [&](E::RowId) { ++listed; });
    check(listed == 3, "rows listed: Kuribo, the copy, and the deleted Coin");
    check(m->undo() && m->state(coin) == E::RowState::Original && m->rowAt(1, 0, 1) == coin, "undo delete restores the row in place");
    check(m->redo() && m->state(coin) == E::RowState::Deleted, "redo delete");

    // Undo the adds: the copy disappears; redo brings it back with the same id and l_id.
    check(m->jumpTo(0) && m->state(copy) == E::RowState::Original && m->objectName(copy) == nullptr, "undo add removes the row");
    check(m->jumpTo(3) && m->state(copy) == E::RowState::Added && m->cell(copy, E::columnHash("l_id"))->i == 8, "redo add, same id");

    // A failure part-way leaves nothing behind.
    E::AddRow good = dup;
    good.id = E::RowId{};
    E::AddRow bad;
    bad.zone = 1;
    bad.table = "jmp/placement/common/nosuchtable";
    const int size = m->historySize();
    check(!m->apply(E::Op{"Bad", {}, {}, {good, bad}, {}}, &error) && m->historySize() == size, "a failed op records nothing");
    int rows = 0;
    m->forEachRow(1, 0, [&](E::RowId) { ++rows; });
    check(rows == 3, "and its first add was taken back");
}

// The Checks engine on the synthetic galaxy (the catalog is the shipped one).
void testChecks() {
    std::map< std::string, F::Bytes > disc = syntheticDisc();
    E::FileSource source = [&](const std::string& path) -> std::optional< F::Bytes > {
        auto it = disc.find(path);
        return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
    };
    std::string error;
    std::unique_ptr< E::EditModel > m = E::EditModel::load("TestGalaxy", 1, source, &error);
    check(PetariEditor::Catalog::load(&error), "catalog: " + error);
    auto find = [](const std::vector< E::Check >& checks, const std::string& prefix) -> const E::Check* {
        for (const E::Check& c : checks)
            if (c.key.rfind(prefix, 0) == 0) return &c;
        return nullptr;
    };
    check(E::computeChecks(*m).empty(), "a clean galaxy has no problems");
    const E::RowId kuribo = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
    const E::RowId coin = findRow(*m, 1, "jmp/placement/layera/objinfo", "Coin");
    auto set = [&](E::RowId row, const char* column, int value) {
        check(m->apply(E::Op{"set", {E::SetCell{row, E::columnHash(column), E::Cell{E::CellType::Int, value, 0, {}}}}, {}}, &error), error);
    };
    auto setName = [&](E::RowId row, const char* name) {
        check(m->apply(E::Op{"name", {E::SetCell{row, E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, name}}}, {}}, &error),
              error);
    };
    set(kuribo, "CommonPath_ID", 3);
    set(kuribo, "SW_APPEAR", 5);
    set(coin, "l_id", 5);
    std::vector< E::Check > checks = E::computeChecks(*m);
    const E::Check* path = find(checks, "dangling-path:");
    check(path && path->severity == E::Severity::Error && path->rows.front() == kuribo && !path->fix.sets.empty(),
          "a path that doesn't exist, with an unlink fix");
    check(find(checks, "duplicate-lid:") != nullptr, "duplicate link ids");
    check(checks.front().severity == E::Severity::Error, "errors first");
    check(m->apply(path->fix, &error) && !find(E::computeChecks(*m), "dangling-path:"), "the fix clears it");
    // A solid object scaled unevenly collides with one scale (CollisionParts::makeEqualScale).
    {
        const E::RowId solid = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
        setName(solid, "StrongBlock");
        set(solid, "scale_x", 0);  // adds the column (Int here; overwritten below as a float)
        check(m->apply(E::Op{"scale", {setFloat(solid, "scale_x", 3.0f)}, {}}, &error), error);
        const std::vector< E::Check > scaled = E::computeChecks(*m);
        const E::Check* uneven = find(scaled, "uneven-collision:");
        check(uneven && !uneven->fix.sets.empty(), "uneven scale on a solid object, with a fix");
        check(m->apply(uneven->fix, &error) && !find(E::computeChecks(*m), "uneven-collision:"), "the fix makes it even");
        PetariEditor::Catalog::ClassInfo brush;  // terrain's brush: its scale is baked into the mesh
        brush.name = "EditorBrush";
        brush.hasCollision = true;
        brush.bakesScale = true;
        PetariEditor::Catalog::addClass(brush);
        setName(solid, "EditorBrush");
        check(m->apply(E::Op{"scale", {setFloat(solid, "scale_y", 0.08f)}, {}}, &error) && !find(E::computeChecks(*m), "uneven-collision:"),
              "terrain brushes are exempt");
        m->undo();
        m->undo();
        m->undo();
        m->undo();
        m->undo();
        m->undo();
    }
    set(kuribo, "SW_B", 500);
    check(find(E::computeChecks(*m), "switch-range:") != nullptr, "a switch id out of range");
    setName(coin, "NoSuchObjectClass");
    check(find(E::computeChecks(*m), "unknown-class:") != nullptr, "an unknown class");
    // Rows in no mission (an unused layer) are not checked.
    const E::RowId takobo = findRow(*m, 1, "jmp/placement/layerb/objinfo", "Takobo");
    set(takobo, "SW_B", 600);
    bool takoboFlagged = false;
    for (const E::Check& c : E::computeChecks(*m))
        for (E::RowId r : c.rows) takoboFlagged = takoboFlagged || r == takobo;
    check(takoboFlagged, "mission 2's rows are checked too (the layer is used by a mission)");
    // Galaxy-wide: 17 Launch Stars in mission 1 is one too many.
    E::Op many{"Launch Stars", {}, {}, {}, {}};
    for (int i = 0; i < 17; i++) {
        E::AddRow add;
        add.zone = 1;
        add.table = "jmp/placement/layera/objinfo";
        add.cells.push_back({E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, "SuperSpinDriver"}});
        many.adds.push_back(add);
    }
    check(m->apply(many, &error), error);
    const std::vector< E::Check > galaxyChecks = E::computeChecks(*m);
    const E::Check* stars = find(galaxyChecks, "launch-stars:1");
    check(stars && stars->rows.size() == 17, "17 Launch Stars in a mission");
    static int sProvided = 0;
    E::registerCheckProvider([](const E::ModelView&, std::vector< E::Check >& out) {
        sProvided++;
        out.push_back(E::Check{"provider-test", E::Severity::Warning, "from a provider", {}, "", E::Op{}, ""});
    });
    check(find(E::computeChecks(*m), "provider-test") != nullptr && sProvided > 0, "check providers");
}

void testModOverDisc() {
    std::map< std::string, F::Bytes > disc = syntheticDisc();
    std::map< std::string, F::Bytes > modded = disc;
    F::Bytes zoneObj = table(kObj, {{str("Kuribo"), i32(5), f32(10), f32(99), f32(30), i32(2)},
                                    {str("Coin"), i32(6), f32(0), f32(0), f32(0), i32(-1)}});
    F::Bytes zoneObjB = table(kObj, {{str("Takobo"), i32(7), f32(0), f32(0), f32(0), i32(-1)}});
    modded["/StageData/TestZone.arc"] =
        F::yaz0Encode(F::Archive::create("stage", {{"jmp/Placement/LayerA/ObjInfo", zoneObj}, {"jmp/Placement/LayerB/ObjInfo", zoneObjB}}).build());
    auto from = [](std::map< std::string, F::Bytes >& files) {
        return [&files](const std::string& path) -> std::optional< F::Bytes > {
            auto it = files.find(path);
            return it == files.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
        };
    };
    std::string error;
    std::unique_ptr< E::EditModel > m = E::EditModel::load("TestGalaxy", 1, from(modded), &error, from(disc));
    check(m != nullptr, "loads over a mod: " + error);
    const E::RowId kuribo = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
    check(m->cell(kuribo, E::columnHash("pos_y"))->f == 99.0f && m->originalCell(kuribo, E::columnHash("pos_y"))->f == 20.0f,
          "current from the mod, original from the disc");
    check(m->state(kuribo) == E::RowState::Changed, "the mod's edit is a change");
    check(m->changedArchives().size() == 1, "an unedited session still saves the mod's archive");
    check(m->apply(E::Op{"Revert", {}, {kuribo}}, &error) && m->changedArchives().empty(), "reverting returns to the disc");
    std::unique_ptr< E::EditModel > plain = E::EditModel::load("TestGalaxy", 1, from(modded), &error);
    check(plain->state(findRow(*plain, 1, "jmp/placement/layera/objinfo", "Kuribo")) == E::RowState::Original,
          "without the disc source the loaded data is the original");

    // A mod that added a row (row counts differ): matched to the disc as the patch writer does,
    // so the mod's row reads Added, the moved Kuribo Changed, and Changes isn't empty on open.
    std::map< std::string, F::Bytes > grown = disc;
    grown["/StageData/TestZone.arc"] = F::Archive::create(
        "stage", {{"jmp/Placement/LayerA/ObjInfo", table(kObj, {{str("Kuribo"), i32(5), f32(10), f32(99), f32(30), i32(2)},
                                                                 {str("StrongBlock"), i32(9), f32(5), f32(5), f32(5), i32(-1)},
                                                                 {str("Coin"), i32(6), f32(0), f32(0), f32(0), i32(-1)}})},
                  {"jmp/Placement/LayerB/ObjInfo", table(kObj, {{str("Takobo"), i32(7), f32(0), f32(0), f32(0), i32(-1)}})}})
                                           .build();
    std::unique_ptr< E::EditModel > g = E::EditModel::load("TestGalaxy", 1, from(grown), &error, from(disc));
    const E::RowId block = findRow(*g, 1, "jmp/placement/layera/objinfo", "StrongBlock");
    const E::RowId kuriboG = findRow(*g, 1, "jmp/placement/layera/objinfo", "Kuribo");
    const E::RowId coinG = findRow(*g, 1, "jmp/placement/layera/objinfo", "Coin");
    check(g->state(block) == E::RowState::Added && g->state(kuriboG) == E::RowState::Changed && g->state(coinG) == E::RowState::Original,
          "the mod's row is Added, its edit Changed, the rest Original");
    check(g->originalCell(kuriboG, E::columnHash("pos_y"))->f == 20.0f, "originals from the matched disc rows");
    check(g->discRowIndex(kuriboG) == 0 && g->discRowIndex(coinG) == 1 && g->discRowIndex(block) == -1,
          "disc row indices (the mod's row has none; Coin moved from 2 back to the disc's 1)");
    const std::string grownPatch = g->patch().json;
    check(grownPatch.find("\"op\": \"add\"") != std::string::npos && grownPatch.find("StrongBlock") != std::string::npos, "and the patch adds it");
}

// Two archives hold the same tables (row order, every cell by column) and the same other files.
bool sameArchive(const F::Bytes& a, const F::Bytes& b, std::string* pWhy) {
    std::optional< F::Archive > x = F::Archive::parse(a), y = F::Archive::parse(b);
    if (!x || !y) return *pWhy = "not archives", false;
    const std::vector< std::string > xFiles = x->files(), yFiles = y->files();
    const std::set< std::string > files(xFiles.begin(), xFiles.end()), other(yFiles.begin(), yFiles.end());
    if (files != other) return *pWhy = "different files", false;
    for (const std::string& path : files) {
        std::optional< F::Table > s = F::Table::parse(*x->file(path)), t = F::Table::parse(*y->file(path));
        if (!s || !t) {
            if (*x->file(path) != *y->file(path)) return *pWhy = path + " differs", false;
            continue;
        }
        if (s->rowCount() != t->rowCount() || s->columns().size() != t->columns().size()) return *pWhy = path + ": rows or columns", false;
        for (int c = 0; c < static_cast< int >(s->columns().size()); c++) {
            const int tc = t->column(s->columns()[c].hash);
            if (tc < 0) return *pWhy = path + ": a column is missing", false;
            for (int r = 0; r < s->rowCount(); r++) {
                if (s->get(r, c) != t->get(r, tc)) return *pWhy = path + " row " + std::to_string(r) + " differs", false;
            }
        }
    }
    return true;
}

float floatIn(const F::Bytes& archive, const char* pTable, const char* pName, const char* pColumn) {
    std::optional< F::Archive > a = F::Archive::parse(archive);
    std::optional< F::Table > t = F::Table::parse(*a->file(pTable));
    const int name = t->column(F::jmapHash("name")), column = t->column(F::jmapHash(pColumn));
    for (int r = 0; r < t->rowCount(); r++)
        if (t->get(r, name).s == pName) return t->get(r, column).f;
    return -12345.0f;
}

// Patch mods: the model's edits as a patch, built onto the disc as the loader does, give the
// tables the model saves itself; a re-save over the built mod gives the same; mods merge; a
// different disc is refused; the loader's cache.
void testPatch() {
    using PetariEditor::Patch::Built;
    std::map< std::string, F::Bytes > disc = syntheticDisc();
    auto from = [](const std::map< std::string, F::Bytes >& files) {
        return [&files](const std::string& path) -> std::optional< F::Bytes > {
            auto it = files.find(path[0] == '/' ? path : "/" + path);
            return it == files.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
        };
    };
    std::string error;
    std::unique_ptr< E::EditModel > m = E::EditModel::load("TestGalaxy", 1, from(disc), &error, from(disc));
    check(m != nullptr && m->patch().json.empty(), "no edits, no patch");
    const E::RowId kuribo = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
    const E::RowId takobo = findRow(*m, 1, "jmp/placement/layerb/objinfo", "Takobo");
    E::Op edits{"Edits", {setFloat(kuribo, "pos_y", 55.0f), E::SetCell{kuribo, E::columnHash("SW_A"), E::Cell{E::CellType::Int, 12, 0, {}}}},
                {}, {}, {takobo}};
    E::AddRow copy;
    copy.zone = 1;
    copy.table = "jmp/placement/layera/objinfo";
    copy.copyOf = kuribo;
    E::AddRow fresh;
    fresh.zone = 1;
    fresh.table = "jmp/placement/layerc/objinfo";
    fresh.cells.push_back({E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, "StarPiece"}});
    edits.adds = {copy, fresh};
    check(m->apply(edits, &error), "edits: " + error);
    const E::EditModel::PatchOut patch = m->patch();
    check(!patch.json.empty() && patch.wholeArchives.empty(), "a patch");
    check(patch.json.find("\"op\": \"delete\"") != std::string::npos && patch.json.find("\"addColumn\"") != std::string::npos &&
              patch.json.find("\"addFile\"") != std::string::npos,
          "delete, addColumn and addFile ops");
    std::vector< std::string > log;
    std::vector< Built > built = PetariEditor::Patch::build({{"A", patch.json}}, from(disc), &log);
    auto changed = m->changedArchives();
    check(built.size() == changed.size() && built.size() == 1, "one archive built");
    check(sameArchive(built[0].bytes, changed[0].second, &error), "the built archive is the model's: " + error);

    // Saving again over the built mod (the editor loads the mod, the disc is the base).
    std::map< std::string, F::Bytes > modded = disc;
    modded["/" + built[0].path] = built[0].bytes;
    std::unique_ptr< E::EditModel > again = E::EditModel::load("TestGalaxy", 1, from(modded), &error, from(disc));
    std::vector< Built > rebuilt = PetariEditor::Patch::build({{"A", again->patch().json}}, from(disc), &log);
    check(rebuilt.size() == 1 && sameArchive(rebuilt[0].bytes, built[0].bytes, &error), "a re-save gives the same archive: " + error);

    // A second mod: merged where they don't overlap, the later (higher priority) one wins where they do.
    std::unique_ptr< E::EditModel > b = E::EditModel::load("TestGalaxy", 1, from(disc), &error, from(disc));
    const E::RowId coinB = findRow(*b, 1, "jmp/placement/layera/objinfo", "Coin");
    const E::RowId kuriboB = findRow(*b, 1, "jmp/placement/layera/objinfo", "Kuribo");
    check(b->apply(E::Op{"B", {setFloat(coinB, "pos_z", 7.0f), setFloat(kuriboB, "pos_y", 1.0f)}, {}}, &error), error);
    std::vector< Built > merged = PetariEditor::Patch::build({{"A", patch.json}, {"B", b->patch().json}}, from(disc), &log);
    check(merged.size() == 1 && merged[0].mods.size() == 2, "both mods in one archive");
    check(floatIn(merged[0].bytes, "jmp/placement/layera/objinfo", "Coin", "pos_z") == 7.0f &&
              floatIn(merged[0].bytes, "jmp/placement/layera/objinfo", "Kuribo", "pos_y") == 1.0f,
          "merged; the later mod wins on the same cell");
    check(F::Archive::parse(merged[0].bytes)->has("jmp/placement/layerc/objinfo"), "and A's new table is kept");

    // Another disc (Kuribo differs): A's rows don't fit, so the archive isn't built from A.
    std::map< std::string, F::Bytes > other = disc;
    other["/StageData/TestZone.arc"] = F::Archive::create(
        "stage", {{"jmp/Placement/LayerA/ObjInfo", table(kObj, {{str("Kuribo"), i32(5), f32(11), f32(20), f32(30), i32(2)},
                                                                 {str("Coin"), i32(6), f32(0), f32(0), f32(0), i32(-1)}})},
                  {"jmp/Placement/LayerB/ObjInfo", table(kObj, {{str("Takobo"), i32(7), f32(0), f32(0), f32(0), i32(-1)}})}})
                                           .build();
    log.clear();
    check(PetariEditor::Patch::build({{"A", patch.json}}, from(other), &log).empty() && !log.empty() &&
              log.back().find("another disc") != std::string::npos,
          "a different disc is refused: " + (log.empty() ? std::string() : log.back()));

    // The loader's step: built into the cache once, then read from it.
    const fs::path root = fs::temp_directory_path() / ("petari-patch-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    for (const auto& [path, bytes] : disc) {
        fs::create_directories((root / "disc/files" / path.substr(1)).parent_path());
        std::ofstream(root / "disc/files" / path.substr(1), std::ios::binary).write(reinterpret_cast< const char* >(bytes.data()), bytes.size());
    }
    fs::create_directories(root / "mods/A/edits");
    std::ofstream(root / "mods/A/edits/TestGalaxy.json") << patch.json;
    log.clear();
    auto first = PetariEditor::Patch::buildForLoader({root / "mods/A"}, root / "disc/files", root / "cache", &log);
    auto second = PetariEditor::Patch::buildForLoader({root / "mods/A"}, root / "disc/files", root / "cache", &log);
    check(first.size() == 1 && first[0].first == "StageData/TestZone.arc" && fs::is_regular_file(first[0].second), "the loader builds");
    check(second.size() == 1 && second[0].second == first[0].second && log.back().find("from the cache") != std::string::npos,
          "and then reads its cache");
    std::ifstream in(first[0].second, std::ios::binary);
    const F::Bytes cached((std::istreambuf_iterator< char >(in)), std::istreambuf_iterator< char >());
    check(sameArchive(cached, built[0].bytes, &error), "the cached archive: " + error);

    // Asset builders (terrain): run after the row edits; a mod one rejects is built without.
    fs::create_directories(root / "mods/Gen/edits");
    fs::create_directories(root / "mods/Bad/edits");
    std::ofstream(root / "mods/Gen/edits/TestGalaxy.json") << patch.json;  // A's edits, as Gen
    std::ofstream(root / "mods/Bad/edits/TestGalaxy.json") << b->patch().json;
    int calls = 0;
    PetariEditor::Patch::registerAssetBuilder("test-1", [&](const fs::path& modRoot, const PetariEditor::Patch::DiscReader&,
                                                            std::vector< Built >* pInOut, std::vector< std::string >*, std::string* pError) {
        calls++;
        if (modRoot.filename() == "Bad") {
            *pError = "a broken brush";
            return false;
        }
        if (modRoot.filename() == "Gen") pInOut->push_back(Built{"ObjectData/Generated.arc", F::Archive::create("generated", {}).build(), {"Gen"}});
        return true;
    });
    log.clear();
    auto generated = PetariEditor::Patch::buildForLoader({root / "mods/Gen", root / "mods/Bad"}, root / "disc/files", root / "cache", &log);
    bool sawGenerated = false, sawZone = false;
    for (const auto& [path, file] : generated) {
        sawGenerated |= path == "ObjectData/Generated.arc" && fs::is_regular_file(file);
        if (path == "StageData/TestZone.arc") {
            std::ifstream zoneIn(file, std::ios::binary);
            const F::Bytes zone((std::istreambuf_iterator< char >(zoneIn)), std::istreambuf_iterator< char >());
            sawZone = floatIn(zone, "jmp/placement/layera/objinfo", "Kuribo", "pos_y") == 55.0f &&
                      floatIn(zone, "jmp/placement/layera/objinfo", "Coin", "pos_z") == 0.0f;  // Bad's edit is left out
        }
    }
    bool saidBad = false;
    for (const std::string& line : log) saidBad |= line.find("mod Bad: not applied: a broken brush") != std::string::npos;
    check(sawGenerated && sawZone && saidBad, "a builder's files are served; a mod it rejects is built without");
    const int before = calls;
    PetariEditor::Patch::buildForLoader({root / "mods/Gen", root / "mods/Bad"}, root / "disc/files", root / "cache", &log);
    check(calls == before, "cached: the builders don't run again");
    fs::create_directories(root / "mods/Gen/assets");
    std::ofstream(root / "mods/Gen/assets/brush.json") << "{}";
    PetariEditor::Patch::buildForLoader({root / "mods/Gen", root / "mods/Bad"}, root / "disc/files", root / "cache", &log);
    check(calls > before, "a changed asset rebuilds");
    fs::remove_all(root);
}

// The galaxy-wide checks the agent tools asked for: a Launch Star facing away from its path, and
// Power Stars sharing a Star ID in a mission.
void testStarChecks() {
    std::map< std::string, F::Bytes > disc = syntheticDisc();
    F::Bytes rails = table({{"name", F::ColumnType::StringOffset}, {"l_id", F::ColumnType::Long}}, {{str("Path"), i32(0)}});
    const std::vector< Col > point = {{"pnt0_x", F::ColumnType::Float}, {"pnt0_y", F::ColumnType::Float}, {"pnt0_z", F::ColumnType::Float}};
    F::Bytes points = table(point, {{f32(0), f32(0), f32(0)}, {f32(1000), f32(0), f32(0)}});
    disc["/StageData/TestZone.arc"] = F::Archive::create(
        "stage", {{"jmp/Placement/LayerA/ObjInfo", table(kObj, {{str("SuperSpinDriver"), i32(5), f32(0), f32(0), f32(0), i32(-1)},
                                                                 {str("PowerStar"), i32(6), f32(0), f32(0), f32(0), i32(3)},
                                                                 {str("PowerStar"), i32(7), f32(9), f32(0), f32(0), i32(3)}})},
                  {"jmp/Placement/LayerB/ObjInfo", table(kObj, {{str("Takobo"), i32(8), f32(0), f32(0), f32(0), i32(-1)}})},
                  {"jmp/Path/CommonPathInfo", rails},
                  {"jmp/Path/CommonPathPointInfo.0", points}})
                                         .build();
    E::FileSource source = [&](const std::string& path) -> std::optional< F::Bytes > {
        auto it = disc.find(path);
        return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
    };
    std::string error;
    std::unique_ptr< E::EditModel > m = E::EditModel::load("TestGalaxy", 1, source, &error);
    check(m != nullptr, "loads with a path: " + error);
    auto find = [](const std::vector< E::Check >& checks, const std::string& prefix) -> const E::Check* {
        for (const E::Check& c : checks)
            if (c.key.rfind(prefix, 0) == 0) return &c;
        return nullptr;
    };
    const std::vector< E::Check > checks = E::computeChecks(*m);
    const E::Check* stars = find(checks, "duplicate-star:1:3");
    check(stars != nullptr && stars->rows.size() == 2 && stars->severity == E::Severity::Warning, "two Power Stars sharing Star ID 3 in mission 1");
    {
        // One placed by the edit: an error (it's killed after a restart), fixed with a free ID.
        E::AddRow star;
        star.id = E::newRowId();
        star.zone = 1;
        star.table = "jmp/placement/layera/objinfo";
        star.cells.push_back({E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, "PowerStar"}});
        star.cells.push_back({E::columnHash("Obj_arg0"), E::Cell{E::CellType::Int, 1, 0, {}}});
        E::AddRow other = star;
        other.id = E::newRowId();
        check(m->apply(E::Op{"Stars", {}, {}, {star, other}, {}}, &error), error);
        const std::vector< E::Check > now = E::computeChecks(*m);
        const E::Check* dup = find(now, "duplicate-star:1:1");
        check(dup && dup->severity == E::Severity::Error && !dup->fix.sets.empty() && dup->fix.sets[0].after.i == 2,
              "a placed star reusing an ID is an error, fixed with Star ID 2");
        m->undo();
    }
    const E::RowId launcher = findRow(*m, 1, "jmp/placement/layera/objinfo", "SuperSpinDriver");
    check(m->apply(E::Op{"On the path", {E::SetCell{launcher, E::columnHash("CommonPath_ID"), E::Cell{E::CellType::Int, 0, 0, {}}}}, {}}, &error),
          error);
    const std::vector< E::Check > facing = E::computeChecks(*m);
    const E::Check* away = find(facing, "launch-star-facing:");
    check(away != nullptr && away->fix.sets.size() == 3, "a Launch Star facing +Z on a path going +X is flagged, with a fix");
    check(m->apply(away->fix, &error) && find(E::computeChecks(*m), "launch-star-facing:") == nullptr, "the fix faces it along the path");
}

// Mission membership (ModelOps): a row moves to the layer with the asked missions, keeping its
// l_id; split copies are one object; capacity follows; impossible and empty masks are refused.
void testMissionOps() {
    namespace O = PetariEditor::ModelOps;
    std::map< std::string, F::Bytes > disc = syntheticDisc();
    E::FileSource source = [&](const std::string& path) -> std::optional< F::Bytes > {
        auto it = disc.find(path);
        return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
    };
    std::string error;
    std::unique_ptr< E::EditModel > m = E::EditModel::load("TestGalaxy", 1, source, &error);
    check(O::layerMissions(*m, 1, 1) == 0b01 && O::layerMissions(*m, 1, 2) == 0b10 && O::layerMissions(*m, 1, 0) == 0b11,
          "layer missions: A = 1, B = 2, Common = both");
    const E::RowId kuribo = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
    const int lid = m->cell(kuribo, E::columnHash("l_id"))->i;
    O::MissionChange toTwo = O::changeMissions(*m, {kuribo}, 0b10);
    check(toTwo.error.empty() && toTwo.rows.size() == 1 && toTwo.op.adds.size() == 1 && toTwo.op.deletes.size() == 1, "to mission 2: one move");
    check(m->apply(toTwo.op, &error), error);
    const E::RowId moved = toTwo.rows[0];
    check(m->rowMissions(moved) == 0b10 && m->cell(moved, E::columnHash("l_id"))->i == lid, "now in mission 2 only, same l_id");
    check(O::changeMissions(*m, {moved}, 0b10).op.adds.empty(), "already there: nothing to do");
    check(!O::changeMissions(*m, {moved}, 0).error.empty(), "no missions is refused (not a delete)");
    check(!O::changeMissions(*m, {moved}, 0b100).error.empty(), "a mission the galaxy doesn't have is refused");
    m->undo();
    check(m->rowMissions(kuribo) == 0b01, "undo puts it back");

    // Split copies (the same object in A and B) are one object: to Common it becomes one row.
    E::AddRow copy;
    copy.id = E::newRowId();
    copy.zone = 1;
    copy.table = "jmp/placement/layerb/objinfo";
    copy.copyOf = kuribo;
    copy.cells.push_back({E::columnHash("l_id"), E::Cell{E::CellType::Int, lid, 0, {}}});
    check(m->apply(E::Op{"Split", {}, {}, {copy}, {}}, &error), error);
    check(O::objectMissions(*m, kuribo) == 0b11 && m->rowMissions(kuribo) == 0b01, "the object is in both missions through its copy");
    O::MissionChange both = O::changeMissions(*m, {kuribo}, 0b11);
    check(both.error.empty() && both.rows.size() == 1 && both.op.deletes.size() == 2, "split copies merge into one Common row");
    check(m->apply(both.op, &error) && m->rowMissions(both.rows[0]) == 0b11, "in both missions once");

    // Capacity follows: 17 Launch Stars in both missions; one moved to mission 1 clears mission 2.
    E::Op many{"Launch Stars", {}, {}, {}, {}};
    for (int i = 0; i < 17; i++) {
        E::AddRow add;
        add.id = E::newRowId();
        add.zone = 1;
        add.table = "jmp/placement/common/objinfo";
        add.cells.push_back({E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, "SuperSpinDriver"}});
        many.adds.push_back(add);
    }
    check(m->apply(many, &error), error);
    auto has = [&](const char* key) {
        for (const E::Check& c : E::computeChecks(*m))
            if (c.key == key) return true;
        return false;
    };
    check(has("launch-stars:1") && has("launch-stars:2"), "17 Launch Stars in each mission");
    O::MissionChange one = O::changeMissions(*m, {many.adds[0].id}, 0b01);
    check(one.error.empty() && m->apply(one.op, &error), "one to mission 1 only: " + one.error);
    check(has("launch-stars:1") && !has("launch-stars:2"), "mission 2 is down to 16");
    m->undo();
    check(has("launch-stars:2"), "undo brings the count back");

    // A free Star ID: the lowest not used by a Power Star in that mission.
    check(O::freeStarId(*m, 1) == 1, "no stars yet: ID 1");
}

void testDisc() {
    const fs::path files = PETARI_TEST_DISC_FILES;
    if (!fs::is_directory(files / "StageData")) {
        std::printf("editor model: no extracted disc; Good Egg checks skipped\n");
        return;
    }
    E::FileSource source = [&](const std::string& path) -> std::optional< F::Bytes > {
        std::ifstream in(files / path.substr(1), std::ios::binary);
        if (!in) return std::nullopt;
        return F::Bytes(std::istreambuf_iterator< char >(in), {});
    };
    std::string error;
    std::unique_ptr< E::EditModel > m = E::EditModel::load("EggStarGalaxy", 1, source, &error);
    check(m != nullptr, "Good Egg loads: " + error);
    int soramame = -1;
    for (int z = 0; z < m->zoneCount(); z++) {
        if (std::string(m->zoneName(z)) == "SoramameZone") soramame = z;
    }
    check(m->zoneCount() == 18 && soramame >= 0 && m->zoneId(soramame) == 14, "Good Egg zones (Soramame is zone 14, as in game)");
    check(m->zonePlaced(soramame) && std::fabs(m->zoneWorldMatrix(soramame).m[0][3] + 18966.7f) < 0.1f,
          "Soramame placed in mission 1 at its StageObjInfo position");
    const E::RowId takobo = findRow(*m, soramame, "jmp/placement/layera/objinfo", "Takobo");
    check(takobo.valid() && (m->activeLayers(soramame, 1) & 0b10), "mission 1 uses Soramame's layer A");
    check(m->changedArchives().empty(), "nothing changed on load");
    int named = 0, total = 0;
    for (int z = 0; z < m->zoneCount(); z++) {
        for (int t = 0; t < m->tableCount(z); t++) {
            for (const E::Column& c : m->columns(z, t)) {
                ++total;
                named += c.name != nullptr;
            }
        }
    }
    check(named * 10 >= total * 9, "at least 90% of Good Egg's columns are named");
    std::printf("editor model: Good Egg %d zones, %d/%d columns named\n", m->zoneCount(), named, total);

    // A patch of real edits (a move, a delete, a duplicate, a new layer's table, every row of a
    // zone touched) builds the archives the model saves.
    std::unique_ptr< E::EditModel > p = E::EditModel::load("EggStarGalaxy", 1, source, &error, source);
    const E::RowId tako = findRow(*p, soramame, "jmp/placement/layera/objinfo", "Takobo");
    const E::RowId coin = findRow(*p, soramame, "jmp/placement/layera/objinfo", "Coin");
    E::Op op{"Edits", {setFloat(tako, "pos_y", p->transform(tako).pos.y + 300.0f)}, {}, {}, {coin}};
    E::AddRow dup;
    dup.zone = soramame;
    dup.table = "jmp/placement/layera/objinfo";
    dup.copyOf = tako;
    E::AddRow fresh;
    fresh.zone = soramame;
    fresh.table = "jmp/placement/layerd/objinfo";
    fresh.copyOf = tako;
    op.adds = {dup, fresh};
    check(p->apply(op, &error), "Good Egg edits: " + error);
    const E::EditModel::PatchOut patch = p->patch();
    std::vector< std::string > log;
    auto built = PetariEditor::Patch::build({{"Test", patch.json}}, [&](const std::string& path) { return source("/" + path); }, &log);
    auto changed = p->changedArchives();
    check(built.size() == 1 && changed.size() == 1 && "/" + built[0].path == changed[0].first, "Good Egg: one archive patched");
    check(sameArchive(built[0].bytes, changed[0].second, &error), "Good Egg: the patch builds the model's archive: " + error);
    check(patch.json.find("Takobo") != std::string::npos && patch.json.size() < 16384, "a small patch (" + std::to_string(patch.json.size()) + " bytes)");
    std::printf("editor model: Good Egg patch %d ops, %zu bytes\n", patch.ops, patch.json.size());
}
}  // namespace

int main() {
    testSynthetic();
    testModOverDisc();
    testStructure();
    testChecks();
    testPatch();
    testStarChecks();
    testMissionOps();
    testDisc();
    std::printf("editor model: %d checks passed\n", checks);
    return 0;
}
