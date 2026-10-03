// Tests for the level editor's EditModel (native/editor/runtime/edit_model.hpp) on a synthetic
// two-zone galaxy built with the editor's own writers: loading, zone placement and layers, cell
// reads, ops, undo/redo/jump, revert, added columns, changed archives and the saved state; and,
// with an extracted disc, Good Egg itself. No game code.

#include "checks.hpp"
#include "edit_model.hpp"
#include <petari/editor/catalog.hpp>
#include <petari/editor/debug_draw.hpp>
#include <petari/editor/patch.hpp>
#include <petari/editor/paths.hpp>
#include <petari/editor/paths_view.hpp>

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

    F::CellValue i32(int v) {
        return F::CellValue{F::CellValue::Kind::Int, v, 0, {}};
    }
    F::CellValue f32(float v) {
        return F::CellValue{F::CellValue::Kind::Float, 0, v, {}};
    }
    F::CellValue str(const char* v) {
        return F::CellValue{F::CellValue::Kind::String, 0, 0, v};
    }

    struct Col {
        const char* name;
        F::ColumnType type;
    };
    F::Bytes table(const std::vector< Col >& cols, const std::vector< std::vector< F::CellValue > >& rows) {
        F::Bytes empty(16, 0);
        empty[11] = 16;  // dataOffset = 16, no fields, no rows
        F::Table t = *F::Table::parse(empty);
        for (const Col& c : cols)
            t.addColumn(F::jmapHash(c.name), c.type, c.type == F::ColumnType::StringOffset ? str("") : i32(0));
        for (const auto& row : rows) {
            const int r = t.addRow();
            for (size_t c = 0; c < row.size(); c++)
                t.set(r, static_cast< int >(c), row[c]);
        }
        return t.build();
    }

    const std::vector< Col > kObj = {{"name", F::ColumnType::StringOffset}, {"l_id", F::ColumnType::Long},   {"pos_x", F::ColumnType::Float},
                                     {"pos_y", F::ColumnType::Float},       {"pos_z", F::ColumnType::Float}, {"Obj_arg0", F::ColumnType::Long}};

    std::map< std::string, F::Bytes > syntheticDisc() {
        std::map< std::string, F::Bytes > disc;
        F::Bytes scenario = table({{"ScenarioNo", F::ColumnType::Long}, {"TestGalaxy", F::ColumnType::Long}, {"TestZone", F::ColumnType::Long}},
                                  {{i32(1), i32(0), i32(1)}, {i32(2), i32(0), i32(2)}});
        F::Bytes zones = table({{"ZoneName", F::ColumnType::StringOffset}}, {{str("TestGalaxy")}, {str("TestZone")}});
        disc["/StageData/TestGalaxy/TestGalaxyScenario.arc"] =
            F::yaz0Encode(F::Archive::create("stage", {{"ScenarioData.bcsv", scenario}, {"ZoneList.bcsv", zones}}).build());
        F::Bytes stageObj = table({{"name", F::ColumnType::StringOffset},
                                   {"l_id", F::ColumnType::Long},
                                   {"pos_x", F::ColumnType::Float},
                                   {"pos_y", F::ColumnType::Float},
                                   {"pos_z", F::ColumnType::Float},
                                   {"dir_x", F::ColumnType::Float},
                                   {"dir_y", F::ColumnType::Float},
                                   {"dir_z", F::ColumnType::Float}},
                                  {{str("TestZone"), i32(0), f32(100), f32(0), f32(0), f32(0), f32(90), f32(0)}});
        F::Bytes galaxyObj = table(kObj, {{str("Coin"), i32(1), f32(1), f32(2), f32(3), i32(-1)}});
        disc["/StageData/TestGalaxy.arc"] =
            F::Archive::create("stage", {{"jmp/Placement/Common/StageObjInfo", stageObj}, {"jmp/Placement/Common/ObjInfo", galaxyObj}}).build();
        F::Bytes zoneObj =
            table(kObj, {{str("Kuribo"), i32(5), f32(10), f32(20), f32(30), i32(2)}, {str("Coin"), i32(6), f32(0), f32(0), f32(0), i32(-1)}});
        F::Bytes zoneObjB = table(kObj, {{str("Takobo"), i32(7), f32(0), f32(0), f32(0), i32(-1)}});
        disc["/StageData/TestZone.arc"] =
            F::Archive::create("stage", {{"jmp/Placement/LayerA/ObjInfo", zoneObj}, {"jmp/Placement/LayerB/ObjInfo", zoneObjB}}).build();
        return disc;
    }

    E::RowId findRow(const E::EditModel& m, int zone, const char* table, const char* name) {
        for (int t = 0; t < m.tableCount(zone); t++) {
            if (std::string(m.tablePath(zone, t)) != table)
                continue;
            E::RowId found;
            m.forEachRow(zone, t, [&](E::RowId id) {
                if (!found.valid() && std::string(m.objectName(id)) == name)
                    found = id;
            });
            return found;
        }
        return {};
    }

    E::SetCell setFloat(E::RowId row, const char* column, float v) {
        return E::SetCell{row, E::columnHash(column), E::Cell{E::CellType::Float, 0, v, {}}};
    }

    void testPaths() {
        namespace P = E::Paths;
        namespace D = E::DebugDraw;
        P::Point a{{1}, {0, 0, 0}, {-5, 0, 0}, {25, 60, 0}}, b{{2}, {100, 0, 0}, {70, -30, 5}, {110, 0, 0}};
        const auto originalA = a, originalB = b;
        const auto mid = P::split(&a, &b, 0.37f);
        for (int i = 0; i <= 100; ++i) {
            const float t = i / 100.0f;
            const auto expected = P::evaluate(originalA, originalB, t);
            const auto actual = t <= 0.37f ? P::evaluate(a, mid, t / 0.37f) : P::evaluate(mid, b, (t - 0.37f) / 0.63f);
            check(E::Gizmo::length(D::vector(expected) - D::vector(actual)) < 0.001f, "split preserves curve");
        }
        auto move = P::movePoint(originalA, {10, 20, 30});
        check(move.sets.size() == 9, "point move includes both handles");
        check(move.sets[3].after.f == 5 && move.sets[7].after.f == 80, "handles translated with point");
        check(P::moveHandle(originalA, true, {1, 2, 3}).sets.size() == 3, "handle independent");

        auto disc = syntheticDisc();
        auto archive = *F::Archive::parse(disc["/StageData/TestGalaxy.arc"]);
        std::vector< std::pair< std::string, F::Bytes > > files;
        for (const auto& path : archive.files())
            files.push_back({path, *archive.file(path)});
        files.push_back(
            {"jmp/path/commonpathinfo", table({{"l_id", F::ColumnType::Long}, {"closed", F::ColumnType::StringOffset}}, {{i32(99), str("CLOSE")}})});
        std::vector< Col > pointCols = {{"id", F::ColumnType::Long}};
        std::vector< std::string > names;
        for (int n = 0; n < 3; ++n)
            for (int axis = 0; axis < 3; ++axis)
                names.push_back("pnt" + std::to_string(n) + "_" + "xyz"[axis]);
        for (const auto& name : names)
            pointCols.push_back({name.c_str(), F::ColumnType::Float});
        std::vector< std::vector< F::CellValue > > pointRows;
        for (int i = 0; i < 3; ++i) {
            std::vector< F::CellValue > values = {i32(i)};
            for (int n = 0; n < 3; ++n)
                for (int axis = 0; axis < 3; ++axis)
                    values.push_back(f32(axis == 0 ? i * 100.0f : 0));
            pointRows.push_back(values);
        }
        files.push_back({"jmp/path/commonpathpointinfo.0", table(pointCols, pointRows)});
        disc["/StageData/TestGalaxy.arc"] = F::Archive::create("stage", files).build();
        E::FileSource source = [&](const std::string& path) -> std::optional< F::Bytes > {
            auto it = disc.find(path);
            return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
        };
        std::string error;
        auto m = E::EditModel::load("TestGalaxy", 1, source, &error);
        check(bool(m), "model loaded");
        E::Catalog::load(&error);
        E::Catalog::ClassInfo writer;
        writer.name = "TestWriter";
        writer.switches = {{"SW_A", E::Catalog::SwitchRole::Write, ""}};
        E::Catalog::addClass(writer);
        E::Catalog::ClassInfo reader;
        reader.name = "TestReader";
        reader.switches = {{"SW_APPEAR", E::Catalog::SwitchRole::Read, ""}};
        E::Catalog::addClass(reader);
        auto object = [&](int zone, const char* name, const char* sw, int id, const char* layer = "common") {
            E::AddRow row;
            row.id = E::newRowId();
            row.zone = zone;
            row.table = std::string("jmp/placement/") + layer + "/objinfo";
            row.cells = {{E::columnHash("name"), E::Cell{E::CellType::StringOffset, 0, 0, name}},
                         {E::columnHash(sw), E::Cell{E::CellType::Int, id, 0, {}}}};
            E::Op op;
            op.label = "Add switch test object";
            op.adds = {row};
            check(m->apply(op, &error), "add object: " + error);
            return row.id;
        };
        const auto output = object(0, "TestWriter", "SW_A", -1), input = object(0, "TestReader", "SW_APPEAR", -1);
        auto socket = [&](E::RowId r) { return P::sockets(*m, r).front(); };
        auto link = P::linkSwitchOp(*m, socket(output), socket(input), &error);
        check(link.sets.size() == 2 && link.sets[0].after.i == 0, "allocate local switch atomically");
        check(m->apply(link, &error), "link apply");
        check(P::wires(*m).size() == 1, "writer to reader wire");
        check(P::computeChecks(*m).empty(), "paired switch has no warning");
        check(m->undo() && m->cell(output, E::columnHash("SW_A"))->i == -1, "link undo restores both endpoints");
        check(m->redo(), "link redo");
        const auto input2 = object(0, "TestReader", "SW_APPEAR", 0);
        const auto cross = object(1, "TestReader", "SW_APPEAR", -1, "layera");
        link = P::linkSwitchOp(*m, socket(output), socket(cross), &error);
        check(link.sets.size() == 4, "cross-zone promotion retains all existing net members");
        check(link.sets[0].after.i == 1000, "cross-zone global id");
        check(m->apply(link, &error), "promote");
        check(m->cell(input2, E::columnHash("SW_APPEAR"))->i == 1000, "other receiver promoted");
        check(P::wires(*m).size() == 3, "promoted wires");
        check(m->apply(P::unlink(output, "SW_A"), &error), "unlink");
        auto problems = P::computeChecks(*m);
        check(problems.size() == 2 && problems[0].rows.size() == 3, "multi-reader missing writer per mission");
        check(m->undo() && P::computeChecks(*m).empty(), "unlink undo fixes checks");
        check(P::linkSwitchOp(*m, socket(input), socket(input2), &error).sets.empty(), "read/read link rejected");
        // A writer present only in mission 2 must not mask mission 1's missing source.
        object(1, "TestWriter", "SW_A", 12, "layerb");
        object(1, "TestReader", "SW_APPEAR", 12, "layera");
        problems = P::computeChecks(*m);
        check(problems.size() == 1 && problems[0].key.find(":12:1") != std::string::npos, "mission-specific writer");
        // Allocation also reserves raw switch numbers on unknown classes.
        object(0, "UnknownClass", "SW_B", 1);
        const auto w2 = object(0, "TestWriter", "SW_A", -1), r2 = object(0, "TestReader", "SW_APPEAR", -1);
        link = P::linkSwitchOp(*m, socket(w2), socket(r2), &error);
        check(link.sets.front().after.i == 0, "freed local ID can be reused after promotion");
        check(m->apply(link, &error), "new local net");
        const auto w3 = object(0, "TestWriter", "SW_A", -1), r3 = object(0, "TestReader", "SW_APPEAR", -1);
        link = P::linkSwitchOp(*m, socket(w3), socket(r3), &error);
        check(link.sets.front().after.i == 2, "raw unknown class ID reserved");

        auto rails = P::collect(*m);
        const auto railId = rails.front().row;
        check(rails.size() == 1 && rails[0].closed && rails[0].points.size() == 3, "rail lookup and loop");
        check(m->railPoints(railId).size() == 3, "ordered rail API");
        check(P::linkPath(*m, output, rails[0], &error).sets.size() == 1, "same-zone path link");
        check(P::linkPath(*m, cross, rails[0], &error).sets.empty(), "cross-zone path link rejected");
        const auto fresh = E::newRowId();
        const auto insert = P::insertPoint(rails[0], 0, 0.5f, fresh, &error);
        check(m->apply(insert, &error), "insert midpoint atomically: " + error);
        check(m->railPoints(railId)[1] == fresh && P::collect(*m)[0].points[1].position.x == 50, "point inserted in order");
        check(m->undo() && m->railPoints(railId).size() == 3, "insertion undo");
        check(m->redo() && m->railPoints(railId)[1] == fresh, "insertion redo retains ID and order");
        const auto patch = m->patch();
        check(patch.json.find("\"at\":1") != std::string::npos || patch.json.find("\"at\": 1") != std::string::npos, "patch stores insertion index");
        std::vector< std::string > patchLog;
        auto rebuilt = E::Patch::build({{"Paths", patch.json}}, [&](const std::string& path) { return source("/" + path); }, &patchLog);
        bool insertedSaved = false;
        for (const auto& file : rebuilt)
            if (file.path == "StageData/TestGalaxy.arc") {
                const auto arc = F::Archive::parse(file.bytes);
                const auto points = F::Table::parse(*arc->file("jmp/path/commonpathpointinfo.0"));
                insertedSaved = points->rowCount() == 4 && points->get(1, points->column(F::jmapHash("pnt0_x"))).f == 50;
            }
        check(insertedSaved, "patch reconstructs inserted point in correct table order");
        check(m->undo(), "undo insertion for next checks");
        const auto before = rails[0].points[1];
        check(m->apply(P::movePoint(before, {140, 25, 0}), &error), "move point");
        check(P::collect(*m)[0].points[1].incoming.y == 25, "model handles moved");
        check(m->undo() && P::collect(*m)[0].points[1].position.x == 100, "move undo");
        E::Op del;
        del.label = "Delete middle point";
        del.deletes = {before.row};
        check(m->apply(del, &error), "delete point");
        check(m->railPoints(railId).size() == 2, "delete preserves remaining order");
        check(m->undo() && m->railPoints(railId)[1] == before.row, "delete undo stable identity");
        const auto createdRail = E::newRowId();
        std::vector< P::Point > newPoints = {{E::newRowId(), {0, 10, 0}, {0, 10, 0}, {0, 10, 0}},
                                             {E::newRowId(), {50, 20, 0}, {50, 20, 0}, {50, 20, 0}}};
        const auto creation = P::addRail(*m, 0, newPoints, false, createdRail, &error);
        check(creation.adds.size() == 3 && m->apply(creation, &error), "create path with fresh indexed point table: " + error);
        auto created = P::collect(*m).back();
        check(created.row == createdRail && created.id == 0 && created.points.size() == 2 && created.points[1].position.x == 50,
              "created path has allocated ID and ordered geometry");
        check(m->undo() && P::collect(*m).size() == 1, "create path undo removes all rows");
        check(m->redo() && P::collect(*m).back().points[0].row == newPoints[0].row, "create path redo keeps identities");
        auto creationPatch = m->patch();
        auto creationFiles = E::Patch::build({{"Paths", creationPatch.json}}, [&](const std::string& path) { return source("/" + path); }, &patchLog);
        bool creationSaved = false;
        for (const auto& file : creationFiles)
            if (file.path == "StageData/TestGalaxy.arc") {
                const auto arc = F::Archive::parse(file.bytes);
                const auto* bytes = arc->file("jmp/path/commonpathpointinfo.1");
                if (bytes) {
                    const auto points = F::Table::parse(*bytes);
                    creationSaved = points && points->rowCount() == 2;
                }
            }
        check(creationSaved, "patch creates numeric-suffix point table from sibling schema");
        check(P::addRail(*m, 0, newPoints, false, E::newRowId(), &error).adds.empty(), "create rejects reused point identities");
        check(m->undo(), "undo creation before remaining checks");
        check(m->apply(P::linkPath(*m, output, P::collect(*m).front(), &error), &error), "set prior rider link");
        const int previousRailId = m->cell(output, E::columnHash("CommonPath_ID"))->i;
        const int draftCursor = m->historyCursor();
        std::vector< P::Point > flightPoints = {{E::newRowId(), {0, 30, 0}, {0, 30, 0}, {0, 30, 0}},
                                                {E::newRowId(), {40, 80, 0}, {40, 80, 0}, {40, 80, 0}},
                                                {E::newRowId(), {80, 30, 0}, {80, 30, 0}, {80, 30, 0}}};
        const auto flightRow = E::newRowId();
        const auto flightOp = P::addRail(*m, 0, flightPoints, false, flightRow, &error, output);
        check(flightOp.adds.size() == 4 && flightOp.sets.size() == 1 && m->apply(flightOp, &error), "create three-point path and link atomically");
        const auto flight = P::collect(*m).back();
        check(m->historyCursor() == draftCursor + 1 && m->cell(output, E::columnHash("CommonPath_ID"))->i == flight.id,
              "new flight rail and link use one history entry");
        check(m->undo() && P::collect(*m).size() == 1 && m->cell(output, E::columnHash("CommonPath_ID"))->i == previousRailId,
              "one undo removes flight and restores prior link");
        check(m->redo() && m->railPoints(flightRow).size() == 3 && m->cell(output, E::columnHash("CommonPath_ID"))->i == flight.id,
              "redo restores flight and link");
        const auto flightPatch = m->patch();
        const auto flightFiles =
            E::Patch::build({{"Paths", flightPatch.json}}, [&](const std::string& path) { return source("/" + path); }, &patchLog);
        bool flightSaved = false;
        for (const auto& file : flightFiles) {
            if (file.path != "StageData/TestGalaxy.arc")
                continue;
            const auto arc = F::Archive::parse(file.bytes);
            const auto* bytes = arc->file("jmp/path/commonpathpointinfo.1");
            if (bytes) {
                const auto table = F::Table::parse(*bytes);
                flightSaved = table && table->rowCount() == 3 && table->get(1, table->column(F::jmapHash("pnt0_y"))).f == 80;
            }
        }
        check(flightSaved, "three-point flight survives patch rebuild");
        check(m->undo() && m->undo(), "restore fixture after flight test");
        for (auto& point : flightPoints)
            point.row = E::newRowId();
        check(P::addRail(*m, 0, flightPoints, false, E::newRowId(), &error, cross).adds.empty(), "reject cross-zone flight link");
        E::PathsView::beginCreate(output);
        E::PathsView::CreateInput draftInput;
        draftInput.hover = true;
        draftInput.x = 0.25f;
        draftInput.add = true;
        E::PathsView::sendCreate(draftInput);
        const auto request = E::PathsView::takeCreate();
        check(E::PathsView::creating() && request.begin && request.add && request.rider == output && request.x == 0.25f,
              "first viewport frame preserves new-path request and rider");
        E::PathsView::endCreate();
        check(!E::PathsView::creating() && !E::PathsView::takeCreate().add, "cancel clears uncommitted requests");
        E::AddRow emptyRail;
        emptyRail.id = E::newRowId();
        emptyRail.zone = 0;
        emptyRail.table = "jmp/path/commonpathinfo";
        emptyRail.cells = {{E::columnHash("l_id"), E::Cell{E::CellType::Int, 98, 0, {}}}};
        E::Op emptyOp;
        emptyOp.adds.push_back(emptyRail);
        check(m->apply(emptyOp, &error), "add empty rail before populated rail");
        for (auto& point : newPoints)
            point.row = E::newRowId();
        const auto followingRail = E::newRowId();
        check(m->apply(P::addRail(*m, 0, newPoints, false, followingRail, &error), &error), "add rail following empty rail");
        const auto withEmpty = P::collect(*m);
        check(withEmpty.size() == 3 && withEmpty[1].row == emptyRail.id && withEmpty[1].points.empty(),
              "collect preserves empty rail in table order");
        check(withEmpty[2].row == followingRail && withEmpty[2].table == "jmp/path/commonpathpointinfo.2" &&
                  withEmpty[2].points.front().row == newPoints.front().row,
              "empty rail does not shift following points");
        check(m->undo() && m->undo(), "undo empty-rail fixture");
        E::Catalog::ClassInfo required = writer;
        required.name = "TestRequiredRail";
        required.links = {{E::Catalog::LinkKind::Rail, "CommonPath_ID", true}};
        E::Catalog::addClass(required);
        const auto missing = object(0, "TestRequiredRail", "SW_A", -1);
        const auto allChecks = E::computeChecks(*m);
        bool missingFound = false;
        for (const auto& c : allChecks)
            if (c.fixKind == "pick-path" && !c.rows.empty() && c.rows.front() == missing)
                missingFound = true;
        check(missingFound, "required missing path uses Runtime Checks engine");
        // Saturating local IDs must fail without a partial endpoint change.
        for (int id = 0; id < 128; ++id)
            object(0, "UnknownClass", "SW_B", id);
        const auto fullWriter = object(0, "TestWriter", "SW_A", -1), fullReader = object(0, "TestReader", "SW_APPEAR", -1);
        error.clear();
        link = P::linkSwitchOp(*m, socket(fullWriter), socket(fullReader), &error);
        check(link.sets.empty() && error.find("128") != std::string::npos, "switch exhaustion is atomic and explained");
    }
}  // namespace
int main() {
    testPaths();
    std::printf("paths: %d checks passed\n", checks);
}
