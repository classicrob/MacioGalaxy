// Tests for the level editor's EditModel (native/editor/runtime/edit_model.hpp) on a synthetic
// two-zone galaxy built with the editor's own writers: loading, zone placement and layers, cell
// reads, ops, undo/redo/jump, revert, added columns, changed archives and the saved state; and,
// with an extracted disc, Good Egg itself. No game code.

#include "edit_model.hpp"

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

    // Moving a zone's StageObjInfo row moves the zone.
    const E::RowId zoneRow = findRow(*m, 0, "jmp/placement/common/stageobjinfo", "TestZone");
    check(m->apply(E::Op{"Move zone", {setFloat(zoneRow, "pos_x", -50.0f)}, {}}, &error) && m->zoneWorldMatrix(1).m[0][3] == -50.0f,
          "zone matrix follows its row");
    check(!m->apply(E::Op{"Bad", {setFloat(E::RowId{12345}, "pos_x", 0)}, {}}, &error) && !error.empty(), "unknown row refused");
    check(m->historySize() == 1, "a refused op records nothing");
    check(m->revision() > 1, "revision bumps");
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
    testDisc();
    std::printf("editor model: %d checks passed\n", checks);
    return 0;
}
