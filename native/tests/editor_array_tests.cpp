#include "edit_model.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <petari/editor/array.hpp>
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
        for (const Col& c : cols) {
            t.addColumn(F::jmapHash(c.name), c.type, c.type == F::ColumnType::StringOffset ? str("") : i32(0));
        }
        for (const auto& row : rows) {
            const int r = t.addRow();
            for (size_t c = 0; c < row.size(); c++) {
                t.set(r, static_cast< int >(c), row[c]);
            }
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
            if (std::string(m.tablePath(zone, t)) != table) {
                continue;
            }
            E::RowId found;
            m.forEachRow(zone, t, [&](E::RowId id) {
                if (!found.valid() && std::string(m.objectName(id)) == name) {
                    found = id;
                }
            });
            return found;
        }
        return {};
    }

}  // namespace
namespace A = E::Arrays;
namespace G = E::Gizmo;
int main() {
    const auto vertical = A::alignAlong({0, 1, 0}, {0, 1, 0});
    check(G::dot(vertical.z, G::V3{0, 1, 0}) > .999f && std::fabs(G::dot(vertical.x, vertical.y)) < 1e-5f,
          "vertical path keeps its full tangent with a finite perpendicular frame");
    const G::Basis frame = G::gravityFrame({1, 0, 0}, {0, 1, 0});
    A::Parameters p;
    p.groundSnap = false;
    p.count = 6;
    p.spacing = 125;
    auto points = A::layout(p, {10, 20, 30}, frame);
    check(points.size() == 6 && G::length(points[0].position - G::V3{10, 145, 30}) < 0.01f, "line follows arbitrary gravity frame");
    check(G::length(points.back().position - G::V3{10, 770, 30}) < 0.01f, "line copies exclude source");
    const G::V3 directions[] = {frame.z, frame.z * -1.f, frame.x, frame.x * -1.f, frame.y, frame.y * -1.f};
    for (int i = 0; i < 6; ++i) {
        p.direction = static_cast< A::Direction >(i);
        const auto axisPoints = A::layout(p, {10, 20, 30}, frame);
        check(G::length(axisPoints.back().position - G::V3{10, 20, 30} - directions[i] * 750.f) < .01f &&
                  G::dot(axisPoints.back().forward, directions[i]) > .999f,
              "visual direction follows local gravity axis " + std::to_string(i));
    }
    p.direction = A::Direction::Forward;
    A::View preview;
    preview.active = preview.hasHandle = true;
    preview.origin = {10, 20, 30};
    preview.handle = {10000, 20, 30};
    A::Copy copy;
    copy.originalPosition = preview.origin;
    copy.position = {8000, 20, 30};
    copy.radius = 100;
    preview.copies.push_back(copy);
    G::V3 center;
    float radius = 0;
    check(A::previewBounds(preview, &center, &radius) && G::length(preview.handle - center) < radius &&
              G::length(copy.originalPosition - center) + copy.radius <= radius + 1 && G::length(copy.position - center) + copy.radius <= radius + 1,
          "frame bounds include source, model extents and offscreen handle");
    preview.active = false;
    check(!A::previewBounds(preview, &center, &radius), "inactive array leaves normal framing alone");
    p.pattern = A::Pattern::Ring;
    p.radius = 300;
    p.facing = A::Facing::Outward;
    points = A::layout(p, {10, 20, 30}, frame);
    for (const auto& q : points) {
        check(std::fabs(G::length(q.position - G::V3{10, 20, 30}) - 300) < 0.01f, "ring radius");
        check(G::dot(q.forward, G::normalized(q.position - G::V3{10, 20, 30})) > 0.999f, "outward facing");
    }
    check(G::length(points.front().position - points.back().position) > 200, "ring excludes duplicate endpoint");
    p.pattern = A::Pattern::Arc;
    p.arcDegrees = 180;
    p.count = 3;
    points = A::layout(p, {}, {});
    check(std::fabs(points.front().position.x - 300) < 0.01f && std::fabs(points.back().position.x + 300) < 0.01f, "arc includes both endpoints");
    p.pattern = A::Pattern::Grid;
    p.rows = 3;
    p.columns = 4;
    points = A::layout(p, {}, {});
    check(points.size() == 12 && points.front().position.z == 125 && points.back().position.x == 375 && points.back().position.z == 375,
          "grid rows and columns");
    E::Paths::Rail rail;
    rail.points = {{{1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{2}, {1000, 0, 0}, {0, 0, 0}, {1000, 0, 0}}};
    E::Mtx34 identity{};
    identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = 1;
    auto path = A::railPolyline(rail, identity);
    p.pattern = A::Pattern::Path;
    p.count = 6;
    points = A::layout(p, {}, {}, &path);
    for (int i = 0; i < 6; ++i) {
        check(std::fabs(points[i].position.x - i * 200.f) < 0.01f, "nonuniform Bezier parameter uses arc length");
    }
    rail.points[0].outgoing = {0, 0, 900};
    rail.points[1].incoming = {1000, 0, 900};
    path = A::railPolyline(rail, identity);
    double reference = 0;
    auto previous = rail.points.front().position;
    for (int i = 1; i <= 10000; ++i) {
        const auto next = E::Paths::evaluate(rail.points[0], rail.points[1], i / 10000.f);
        reference += G::length(E::DebugDraw::vector(next) - E::DebugDraw::vector(previous));
        previous = next;
    }
    check(std::fabs(path.length() - reference) < 2, "curved rail arc length agrees with dense integration");
    points = A::layout(p, {}, {}, &path);
    check(std::fabs(points[0].position.x) < 0.01f && std::fabs(points[5].position.x - 1000) < 0.01f, "open rail includes endpoints");
    p.pathSpacing = true;
    p.spacing = 200;
    p.startDistance = 100;
    points = A::layout(p, {}, {}, &path);
    check(points.size() == static_cast< size_t >(1 + std::floor((path.length() - 100) / 200)), "spacing mode derives count from remaining distance");
    check(G::length(points.front().position - A::atDistance(path, 100).position) < 0.01f, "path start distance");
    p.startDistance = path.length();
    p.pathSpacing = false;
    points = A::layout(p, {}, {}, &path);
    check(points.size() == 1 && G::length(points.front().position - path.points.back()) < .01f,
          "starting at the rail end makes one copy, not coincident duplicates");
    rail.closed = true;
    path = A::railPolyline(rail, identity);
    p.pathSpacing = false;
    p.startDistance = 0;
    p.count = 12;
    points = A::layout(p, {}, {}, &path);
    check(points.size() == 12 && G::length(points.front().position - points.back().position) > 1, "closed rail has no duplicate seam copy");
    p.startDistance = path.length() + 1;
    check(A::layout(p, {}, {}, &path).empty(), "out-of-range start has no copies");
    rail.points.clear();
    path = A::railPolyline(rail, identity);
    check(A::layout(p, {}, {}, &path).empty(), "empty rail safe");
    p.spacing = std::numeric_limits< float >::quiet_NaN();
    p.radius = -2;
    p.count = 99999;
    p = A::bounded(p);
    check(p.count == 256 && p.radius == 1 && p.spacing == 150, "invalid inputs bounded");

    auto disc = syntheticDisc();
    auto source = [&](const std::string& name) -> std::optional< F::Bytes > {
        auto it = disc.find(name);
        return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
    };
    std::string error;
    auto m = E::EditModel::load("TestGalaxy", 1, source, &error);
    check(bool(m), "fixture loaded");
    auto row = findRow(*m, 1, "jmp/placement/layera/objinfo", "Kuribo");
    check(row.valid(), "child-zone source");
    std::vector< A::Copy > copies;
    std::vector< E::RowId > ids;
    for (int i = 0; i < 4; ++i) {
        A::Copy copy;
        copy.source = row;
        copy.position = {300.f + i * 100, 100, 200};
        copy.basis = G::fromEulerDegrees({0, 30, 0});
        copies.push_back(copy);
        ids.push_back(E::newRowId());
    }
    const int cursor = m->historyCursor();
    const auto op = A::makeOp(*m, copies, ids, &error);
    check(op.adds.size() == 4 && m->historyCursor() == cursor, "preview op construction does not edit model");
    check(m->apply(op, &error), "array commit: " + error);
    check(m->historyCursor() == cursor + 1, "one array is one undo step");
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto t = m->transform(ids[i]);
        const auto world = E::DebugDraw::transform(m->zoneWorldMatrix(1), t.pos);
        check(G::length(E::DebugDraw::vector(world) - copies[i].position) < 0.01f, "world copies converted through rotated child zone");
        check(m->cell(ids[i], E::columnHash("Obj_arg0"))->i == 2 && m->rowMissions(ids[i]) == m->rowMissions(row), "settings and mission inherited");
        check(std::fabs(t.dirDeg.y + 60) < 0.01f, "world heading converted to child zone");
    }
    check(m->undo() && m->historyCursor() == cursor, "single undo removes array");
    for (auto id : ids) {
        check(!m->locate(id, nullptr, nullptr), "every copy removed");
    }
    check(m->redo() && m->historyCursor() == cursor + 1, "single redo restores array");
    check(A::makeOp(*m, copies, {ids[0]}, &error).adds.empty(), "mismatched fresh IDs rejected atomically");
    std::printf("PASS: %d array checks\n", checks);
}
