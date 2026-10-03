#include "edit_model.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <petari/editor/areas.hpp>
#include <petari/editor/catalog.hpp>
namespace E = PetariEditor;
namespace F = E::Formats;
namespace A = E::Areas;
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

}  // namespace
int main() {
    A::Volume box;
    box.definition = A::definition("CubeCameraBox");
    box.size = {500, 1000, 200};
    check(box.definition && box.definition->capacity == 160, "camera manager cap");
    check(A::contains(box, {499, 999, 199}) && !A::contains(box, {501, 0, 0}), "centred box bounds");
    A::Volume base = box;
    base.definition = A::definition("DeathCube");
    check(A::contains(base, {0, 1500, 0}) && !A::contains(base, {0, -1, 0}), "base cube origin");
    A::Volume cylinder = box;
    cylinder.definition = A::definition("WaterCylinder");
    check(A::contains(cylinder, {0, 999, 0}) && !A::contains(cylinder, {0, 1001, 0}), "cylinder height is scale_y times 500");
    A::Volume bowl = box;
    bowl.definition = A::definition("CubeCameraBowl");
    check(A::contains(bowl, {0, -200, 0}) && !A::contains(bowl, {0, 200, 0}), "bowl lower hemisphere");
    A::Volume sphere = box;
    sphere.definition = A::definition("DeathSphere");
    check(!A::contains(sphere, {0, 501, 0}), "sphere uses only x scale");
    for (auto shape : {box, base, cylinder, bowl, sphere}) {
        const auto mesh = A::geometry(shape, true);
        check(!mesh.lines.empty() && !mesh.triangles.empty(), "shape has outline and fill");
        auto other = shape;
        other.origin = {100, 0, 0};
        check(A::overlaps(shape, other), "intersecting convex forms");
        other.origin = {5000, 0, 0};
        check(!A::overlaps(shape, other), "separated convex forms");
    }
    auto slender = box;
    slender.size = {1000, 10, 10};
    auto near = slender;
    near.origin = {0, 100, 0};
    check(!A::overlaps(slender, near), "overlapping bounding spheres do not imply box overlap");
    near.axes = E::Gizmo::fromEulerDegrees({0, 0, 90});
    check(A::overlaps(slender, near), "rotated box intersection");
    for (int x = -4; x <= 4; ++x)
        for (int y = -4; y <= 4; ++y)
            for (int z = -2; z <= 2; ++z) {
                auto other = box;
                other.origin = {x * 310.f, y * 310.f, z * 310.f};
                const bool expected = std::fabs(other.origin.x) <= box.size.x * 2 && std::fabs(other.origin.y) <= box.size.y * 2 &&
                                      std::fabs(other.origin.z) <= box.size.z * 2;
                check(A::overlaps(box, other) == expected, "GJK agrees with axis-aligned box intersection");
            }
    check(A::definition("PipeModeCube")->capacity == 4 && A::definition("MercatorCube")->capacity == 1, "small manager capacities");
    const std::vector< Col > cols = {{"name", F::ColumnType::StringOffset}, {"pos_x", F::ColumnType::Float},   {"pos_y", F::ColumnType::Float},
                                     {"pos_z", F::ColumnType::Float},       {"dir_y", F::ColumnType::Float},   {"scale_x", F::ColumnType::Float},
                                     {"scale_y", F::ColumnType::Float},     {"scale_z", F::ColumnType::Float}, {"Obj_arg0", F::ColumnType::Long},
                                     {"Obj_arg2", F::ColumnType::Long},     {"Obj_arg3", F::ColumnType::Long}};
    auto row = [&](const char* name, float x) {
        return std::vector< F::CellValue >{str(name), f32(x), f32(0), f32(0), f32(0), f32(1), f32(1), f32(1), i32(0), i32(5), i32(0)};
    };
    std::map< std::string, F::Bytes > disc;
    disc["/StageData/TestGalaxy/TestGalaxyScenario.arc"] =
        F::Archive::create(
            "scenario", {{"ScenarioData.bcsv", table({{"ScenarioNo", F::ColumnType::Long}, {"TestGalaxy", F::ColumnType::Long}}, {{i32(1), i32(0)}})},
                         {"ZoneList.bcsv", table({{"ZoneName", F::ColumnType::StringOffset}}, {{str("TestGalaxy")}})}})
            .build();
    disc["/StageData/TestGalaxy.arc"] =
        F::Archive::create(
            "stage", {{"jmp/Placement/Common/CameraCubeInfo", table(cols, {row("CubeCameraBox", 0), row("CubeCameraSphere", 100)})},
                      {"jmp/Placement/Common/AreaObjInfo", table(cols, {row("DeathCube", 0), row("MercatorCube", 2000), row("MercatorCube", 3000)})},
                      {"jmp/Start/Common/StartInfo", table(cols, {row("Mario", 0)})},
                      {"camera/CameraParam.bcam", table({{"id", F::ColumnType::StringOffset}}, {{str("c:0000")}})}})
            .build();
    std::string error;
    auto m = E::EditModel::load(
        "TestGalaxy", 1,
        [&](const std::string& path) -> std::optional< F::Bytes > {
            auto it = disc.find(path);
            return it == disc.end() ? std::nullopt : std::optional< F::Bytes >(it->second);
        },
        &error);
    check(bool(m), "load area fixture: " + error);
    auto volumes = A::collect(*m);
    check(volumes.size() == 5, "collect area and camera tables");
    std::vector< E::Check > out;
    A::computeChecks(*m, out);
    auto has = [&](const char* prefix) { return std::any_of(out.begin(), out.end(), [&](const auto& c) { return c.key.rfind(prefix, 0) == 0; }); };
    check(has("area-camera-overlap:"), "same-priority overlap Check");
    check(has("area-death-start:"), "death area at start Check");
    check(has("area-capacity:MercatorCube"), "manager capacity Check");
    check(!has("area-camera-missing:"), "camera parameter link resolves");
    E::RowId camera;
    for (const auto& v : volumes)
        if (v.definition->shape == A::Shape::Sphere && v.definition->kind == A::Kind::Camera)
            camera = v.row;
    E::Op op;
    op.label = "Change camera priority";
    op.sets.push_back({camera, E::columnHash("Obj_arg2"), {E::CellType::Int, 6, 0, {}}});
    check(m->apply(op, &error), "edit camera priority");
    out.clear();
    A::computeChecks(*m, out);
    check(!has("area-camera-overlap:"), "different priorities do not warn");
    check(m->undo(), "undo area edit");
    out.clear();
    A::computeChecks(*m, out);
    check(has("area-camera-overlap:"), "undo restores overlap Check");
    const auto param = A::cameraRow(*m, volumes.front());
    E::Op cameraEdit;
    cameraEdit.label = "Rename camera parameter";
    cameraEdit.sets.push_back({param, E::columnHash("id"), {E::CellType::StringOffset, 0, 0, "c:0001"}});
    check(m->apply(cameraEdit, &error), "edit non-jmp camera table");
    const auto archives = m->changedArchives();
    check(archives.size() == 1, "camera edit produces archive override");
    const auto saved = F::Archive::parse(archives.front().second);
    const auto cameraTable = F::Table::parse(*saved->file("camera/cameraparam.bcam"));
    check(cameraTable->get(0, cameraTable->column(E::columnHash("id"))).s == "c:0001", "camera parameters survive archive save");
    check(m->patch().json.find("camera/cameraparam.bcam") != std::string::npos, "camera parameters included in patch");
    check(m->undo(), "undo camera parameter edit");
    check(A::cameraRow(*m, volumes.front()).valid(), "camera parameter undo restores link");
    check(m->changedArchives().empty(), "undone bcam edit produces no changed archive");
    std::printf("areas: %d checks passed\n", checks);
}
