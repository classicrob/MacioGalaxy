#include "edit_model.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <petari/editor/gravity.hpp>
#include <petari/editor/paths.hpp>
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
    auto table = [](std::vector< std::pair< const char*, F::CellValue > > values, bool populated = true) {
        F::Bytes raw(16, 0);
        raw[11] = 16;
        auto t = *F::Table::parse(raw);
        for (auto& v : values)
            t.addColumn(F::jmapHash(v.first), v.second.kind == F::CellValue::Kind::String ? F::ColumnType::StringOffset : F::ColumnType::Long,
                        v.second);
        if (populated) {
            const int row = t.addRow();
            for (std::size_t column = 0; column < values.size(); ++column)
                t.set(row, static_cast< int >(column), values[column].second);
        }
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
                                      table({{"name", str("GlobalPointGravity")}, {"Priority", i(0)}, {"CommonPath_ID", i(-1)}})},
                                  {"jmp/path/commonpathinfo", table({{"l_id", i(0)}, {"no", i(0)}, {"num_pnt", i(0)}, {"type", str("Bezier")}, {"closed", str("OPEN")}}, false)},
                                  {"jmp/path/commonpathpointinfo.0", table({{"id", i(0)}, {"pnt0_x", i(0)}, {"pnt0_y", i(0)}, {"pnt0_z", i(0)},
                                                                         {"pnt1_x", i(0)}, {"pnt1_y", i(0)}, {"pnt1_z", i(0)},
                                                                         {"pnt2_x", i(0)}, {"pnt2_y", i(0)}, {"pnt2_z", i(0)}}, false)}})
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
    for (int t = 0; t < m->tableCount(0); ++t)
        m->forEachRow(0, t, [&](RowId r) { if (G::shapeForPlacement(m->objectName(r)) != G::Shape::Unknown) row = r; });
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
    require(G::shapeOp(*m, row, G::Shape::Wire).sets.empty(), "wire rejects a missing path");
    RowId railId = newRowId();
    std::vector<Paths::Point> points = {{newRowId(), {0,0,0}, {0,0,0}, {0,0,0}},
                                      {newRowId(), {100,0,0}, {100,0,0}, {100,0,0}}};
    require(m->apply(Paths::addRail(*m, 0, points, false, railId, nullptr), nullptr), "wire fixture path added");
    auto rails = Paths::collect(*m);
    require(rails.size() == 1, "wire fixture path collected");
    require(m->apply(Paths::linkPath(*m, row, rails.front(), nullptr), nullptr), "wire fixture linked");
    require(!G::shapeOp(*m, row, G::Shape::Wire).sets.empty(), "wire accepts two-point path");
    Op remove;
    remove.deletes.push_back(points.front().row);
    require(m->apply(remove, nullptr), "wire fixture point removed");
    require(G::shapeOp(*m, row, G::Shape::Wire).sets.empty(), "wire rejects one-point path");
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
        require(mesh.triangles.empty(), "unfocused fields never wash out the scene");
        mesh = G::geometry(f, true);
        require(!mesh.triangles.empty(), "selected field has faint translucent faces");
        require((mesh.triangles.front().color >> 24) <= 10, "focused fill stays faint");
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
    point.row = {17};
    const auto hue = G::fieldColor(point) & 0xffffff;
    point.priority = 123; point.range = 99; point.identity = 9876;
    require((G::fieldColor(point) & 0xffffff) == hue, "field hue survives parameter and runtime identity changes");
    point.row = {18};
    require((G::fieldColor(point) & 0xffffff) != hue, "distinct fields receive distinct hues");
    require(G::rangeFade(100, 0, 100) == 0 && G::rangeFade(50, 0, 100) == 1, "finite range fades to zero at boundary");
    require(G::rangeFade(90, 0, 100) > 0 && G::rangeFade(90, 0, 100) < 1, "range fade is gradual");
    require(G::rangeFade(10000, 0, -1) == 1, "unlimited range never fades out");
    G::Field parallel;
    parallel.shape=G::Shape::Parallel; parallel.range=100;
    require(G::flowFade(parallel,{0,0,0},2000)==1, "parallel sphere fades by boundary, not force distance");
    require(G::flowFade(parallel,{100,0,0},2000)==0, "parallel sphere boundary fades out");
    parallel.bound=G::Bound::Box; parallel.x={100,0,0}; parallel.y={0,200,0}; parallel.z={0,0,100};
    require(G::flowFade(parallel,{0,0,0},2000)==1 && G::flowFade(parallel,{0,200,0},2000)==0, "box fade follows scaled extents");
    parallel.bound=G::Bound::Cylinder; parallel.radius=100; parallel.height=200; parallel.y={0,1,0};
    require(G::flowFade(parallel,{0,100,0},2000)==1 && G::flowFade(parallel,{100,100,0},2000)==0, "cylinder fades at radial boundary");
    require(G::flowFade(parallel,{0,0,0},2000)==0 && G::flowFade(parallel,{0,200,0},2000)==0, "cylinder fades at both ends");
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
    c = G::makeChecks(v, {{7}});
    require(c.size() == 1 && c[0].rows.front().v == 8, "vanilla sampled holes stay baseline while new ground remains visible");
    G::publish(v);
    require(G::view().registered == 5, "snapshot round trip");
    G::reset();
    require(G::view().deadGround.empty(), "reset removes stale scene samples");
    std::printf("editor gravity: %d checks passed\n", checkCount);
}
