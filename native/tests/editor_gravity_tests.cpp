#include "edit_model.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <petari/editor/gravity.hpp>
using namespace PetariEditor;
namespace G = Gravity;
static int checkCount = 0;
void require(bool condition, const char* label) {
    checkCount++;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}
bool finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
void modelChecks() {
    namespace F = Formats;
    auto table = [](std::vector< std::pair< const char*, F::CellValue > > values) {
        F::Bytes raw(16, 0);
        raw[11] = 16;
        auto t = *F::Table::parse(raw);
        for (auto& v : values)
            t.addColumn(F::jmapHash(v.first), v.second.kind == F::CellValue::Kind::String ? F::ColumnType::StringOffset : F::ColumnType::Long,
                        v.second);
        const int row = t.addRow();
        for (std::size_t column = 0; column < values.size(); ++column)
            t.set(row, static_cast< int >(column), values[column].second);
        return t.build();
    };
    auto i = [](int value) { return F::CellValue{F::CellValue::Kind::Int, value, 0, {}}; };
    auto str = [](const char* value) { return F::CellValue{F::CellValue::Kind::String, 0, 0, value}; };
    std::map< std::string, F::Bytes > files;
    files["/StageData/TestGalaxy/TestGalaxyScenario.arc"] =
        F::Archive::create("stage", {{"ScenarioData.bcsv", table({{"ScenarioNo", i(1)}, {"TestGalaxy", i(0)}})},
                                     {"ZoneList.bcsv", table({{"ZoneName", str("TestGalaxy")}})}})
            .build();
    files["/StageData/TestGalaxy.arc"] =
        F::Archive::create("stage", {{"jmp/placement/common/planetobjinfo",
                                      table({{"name", str("GlobalPointGravity")}, {"Priority", i(0)}, {"CommonPath_ID", i(-1)}})}})
            .build();
    auto m = EditModel::load(
        "TestGalaxy", 1,
        [&](const std::string& path) -> std::optional< F::Bytes > {
            auto it = files.find(path);
            return it == files.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
        },
        nullptr);
    require(m != nullptr, "gravity model loaded");
    RowId row;
    m->forEachRow(0, 0, [&](RowId r) { row = r; });
    require(row.valid(), "gravity row loaded");
    auto op = G::numericOp(*m, {row}, "Priority", 7, true);
    require(m->apply(op, nullptr), "priority op applied");
    require(m->cell(row, columnHash("Priority"))->i == 7, "priority persisted as int");
    require(m->undo() && m->cell(row, columnHash("Priority"))->i == 0, "priority undo");
    op = G::numericOp(*m, {row}, "Range", -50);
    require(m->apply(op, nullptr), "range column added");
    require(m->cell(row, columnHash("Range"))->f == -1, "negative range normalized to infinity");
    require(G::numericOp(*m, {row}, "Range", std::numeric_limits< float >::infinity()).sets.empty(), "reject nonfinite range");
    op = G::shapeOp(*m, row, G::Shape::Cube);
    require(m->apply(op, nullptr), "shape op applied");
    require(std::string(m->objectName(row)) == "GlobalCubeGravity", "shape placement persisted");
    require(m->cell(row, columnHash("Obj_arg0"))->i == -1, "new shape enables all default cube faces");
    require(m->undo() && std::string(m->objectName(row)) == "GlobalPointGravity", "shape undo is atomic");
}
int main() {
    modelChecks();
    for (int i = 0; i < 8; i++) {
        auto shape = static_cast< G::Shape >(i);
        require(G::shapeForPlacement(G::placementName(shape)) == shape, "placement shape round trip");
        G::Field f;
        f.shape = shape;
        f.radius = 500;
        f.height = 800;
        f.range = 1500;
        f.distant = 100;
        f.innerRadius = 300;
        f.x = {500, 0, 0};
        f.y = {0, 500, 0};
        f.z = {0, 0, 500};
        f.points = {{0, 0, 0}, {100, 400, 200}, {500, 600, 300}};
        auto mesh = G::geometry(f);
        require(!mesh.lines.empty(), "every shape has outlines");
        require(!mesh.triangles.empty(), "every shape has translucent faces");
        for (auto& l : mesh.lines)
            require(finite(l.a) && finite(l.b), "finite outlines");
        for (auto& t : mesh.triangles)
            require(finite(t.a) && finite(t.b) && finite(t.c), "finite faces");
    }
    G::Field point;
    point.shape = G::Shape::Point;
    point.range = 100;
    point.distant = 50;
    auto mesh = G::geometry(point);
    bool reach = false;
    for (auto& l : mesh.lines)
        if (std::fabs(l.a.x - 150) < 0.001f)
            reach = true;
    require(reach, "point reach includes Distant");
    G::View v;
    v.registered = 115;
    require(G::makeChecks(v).empty(), "below near cap");
    v.registered = 116;
    auto c = G::makeChecks(v);
    require(c.size() == 1 && c[0].severity == Severity::Warning, "near cap warning");
    v.registered = 128;
    c = G::makeChecks(v);
    require(c.size() == 1 && c[0].severity == Severity::Error, "full cap error");
    v.registered = 5;
    v.deadGround = {{{7}, {1, 2, 3}, {0, 1, 0}}, {{7}, {4, 5, 6}, {0, 1, 0}}, {{8}, {7, 8, 9}, {0, 1, 0}}};
    c = G::makeChecks(v);
    require(c.size() == 2, "deduplicate ground warning per row");
    require(c[0].rows.front().v == 7, "ground warning selects owner");
    G::publish(v);
    require(G::view().registered == 5, "snapshot round trip");
    G::reset();
    require(G::view().deadGround.empty(), "reset removes stale scene samples");
    std::printf("editor gravity: %d checks passed\n", checkCount);
}
