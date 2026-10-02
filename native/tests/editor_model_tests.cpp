// Tests for the level editor's EditModel (native/editor/runtime/edit_model.hpp) on a synthetic
// two-zone galaxy built with the editor's own writers: loading, zone placement and layers, cell
// reads, ops, undo/redo/jump, revert, added columns, changed archives and the saved state; and,
// with an extracted disc, Good Egg itself. No game code.

#include "checks.hpp"
#include "edit_model.hpp"
#include <petari/editor/catalog.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

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
    check(find(checks, "switch-alone:") != nullptr, "a switch only one row uses");
    check(find(checks, "duplicate-lid:") != nullptr, "duplicate link ids");
    check(checks.front().severity == E::Severity::Error, "errors first");
    check(m->apply(path->fix, &error) && !find(E::computeChecks(*m), "dangling-path:"), "the fix clears it");
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
}
}  // namespace

int main() {
    testSynthetic();
    testModOverDisc();
    testStructure();
    testChecks();
    testDisc();
    std::printf("editor model: %d checks passed\n", checks);
    return 0;
}
