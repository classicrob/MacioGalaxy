#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <petari/editor/gravity.hpp>
#include <set>

namespace PetariEditor::Gravity {
    namespace {
        std::atomic< bool > shown{false}, air{false};
        std::mutex lock;
        View current;
        constexpr float pi = 3.14159265358979323846f;
        Vec3 add(Vec3 a, Vec3 b) {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }
        Vec3 mul(Vec3 a, float f) {
            return {a.x * f, a.y * f, a.z * f};
        }
        float length(Vec3 a) {
            return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        }
        Vec3 unit(Vec3 a) {
            float l = length(a);
            return l > 0.0001f ? mul(a, 1 / l) : Vec3{0, 1, 0};
        }
        Vec3 cross(Vec3 a, Vec3 b) {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }
        void basis(Vec3 n, Vec3& x, Vec3& z) {
            n = unit(n);
            x = unit(cross(n, std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
            z = cross(x, n);
        }
        void ring(Mesh& m, Vec3 center, Vec3 x, Vec3 z, float radius, unsigned c, bool dash = false, float degrees = 360) {
            for (int i = 0; i < 32; i++) {
                if (dash && i % 2)
                    continue;
                float a = degrees * pi / 180 * i / 32, b = degrees * pi / 180 * (i + 1) / 32;
                m.lines.push_back({add(center, add(mul(x, radius * std::cos(a)), mul(z, radius * std::sin(a)))),
                                   add(center, add(mul(x, radius * std::cos(b)), mul(z, radius * std::sin(b)))), c});
            }
        }
        void sphere(Mesh& m, Vec3 p, float r, unsigned c, bool dash, bool fill) {
            ring(m, p, {1, 0, 0}, {0, 1, 0}, r, c, dash);
            ring(m, p, {1, 0, 0}, {0, 0, 1}, r, c, dash);
            ring(m, p, {0, 1, 0}, {0, 0, 1}, r, c, dash);
            if (!fill)
                return;
            auto point = [&](int a, int b) {
                float lat = pi * a / 8, lon = 2 * pi * b / 16;
                return add(p, {r * std::sin(lat) * std::cos(lon), r * std::cos(lat), r * std::sin(lat) * std::sin(lon)});
            };
            for (int a = 0; a < 8; a++)
                for (int b = 0; b < 16; b++) {
                    auto p0 = point(a, b), p1 = point(a + 1, b), p2 = point(a + 1, b + 1), p3 = point(a, b + 1);
                    m.triangles.push_back({p0, p1, p2, (c & 0xffffff) | 0x12000000});
                    m.triangles.push_back({p0, p2, p3, (c & 0xffffff) | 0x12000000});
                }
        }
        void box(Mesh& m, const Field& f, unsigned c) {
            Vec3 p[8];
            for (int i = 0; i < 8; i++)
                p[i] = add(f.center, add(mul(f.x, i & 1 ? 1 : -1), add(mul(f.y, i & 2 ? 1 : -1), mul(f.z, i & 4 ? 1 : -1))));
            for (int i = 0; i < 8; i++)
                for (int j = 0; j < 3; j++)
                    if (!(i & (1 << j)))
                        m.lines.push_back({p[i], p[i | (1 << j)], c});
            const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
            for (auto& q : faces) {
                m.triangles.push_back({p[q[0]], p[q[1]], p[q[2]], (c & 0xffffff) | 0x16000000});
                m.triangles.push_back({p[q[0]], p[q[2]], p[q[3]], (c & 0xffffff) | 0x16000000});
            }
        }
        void tube(Mesh& m, Vec3 p, Vec3 q, float r0, float r1, unsigned c, bool fill = true, bool dashed = false) {
            Vec3 axis{q.x - p.x, q.y - p.y, q.z - p.z}, x, z;
            basis(axis, x, z);
            ring(m, p, x, z, r0, c, dashed);
            ring(m, q, x, z, r1, c, dashed);
            for (int i = 0; i < 32; i++) {
                float a = 2 * pi * i / 32, b = 2 * pi * (i + 1) / 32;
                Vec3 va = add(mul(x, std::cos(a)), mul(z, std::sin(a))), vb = add(mul(x, std::cos(b)), mul(z, std::sin(b)));
                Vec3 p0 = add(p, mul(va, r0)), p1 = add(p, mul(vb, r0)), q0 = add(q, mul(va, r1)), q1 = add(q, mul(vb, r1));
                if (i % 8 == 0)
                    m.lines.push_back({p0, q0, c});
                if (fill) {
                    m.triangles.push_back({p0, p1, q1, (c & 0xffffff) | 0x16000000});
                    m.triangles.push_back({p0, q1, q0, (c & 0xffffff) | 0x16000000});
                }
            }
        }
    }  // namespace
    const char* shapeName(Shape s) {
        static const char* names[] = {"Point", "Cube", "Cone", "Disk", "Torus", "Parallel", "Segment", "Wire", "Unknown"};
        return names[static_cast< int >(s)];
    }
    const char* placementName(Shape s, Bound bound) {
        if (s == Shape::Parallel)
            return bound == Bound::Box ? "GlobalPlaneGravityInBox" : bound == Bound::Cylinder ? "GlobalPlaneGravityInCylinder" : "GlobalPlaneGravity";
        static const char* names[] = {"GlobalPointGravity",   "GlobalCubeGravity",      "GlobalConeGravity",
                                      "GlobalDiskGravity",    "GlobalDiskTorusGravity", "GlobalPlaneGravity",
                                      "GlobalSegmentGravity", "GlobalWireGravity",      ""};
        return names[static_cast< int >(s)];
    }
    Shape shapeForPlacement(const char* name) {
        if (!name)
            return Shape::Unknown;
        for (int s = 0; s < 8; s++)
            if (std::strcmp(name, placementName(static_cast< Shape >(s))) == 0)
                return static_cast< Shape >(s);
        if (std::strcmp(name, "GlobalPlaneGravityInBox") == 0 || std::strcmp(name, "GlobalPlaneGravityInCylinder") == 0)
            return Shape::Parallel;
        return Shape::Unknown;
    }
    std::uint32_t color(Shape s, unsigned alpha) {
        static const unsigned colors[] = {0xecc46c, 0x78df94, 0x74a9ef, 0xe4a97c, 0xe792c7, 0x94dcdc, 0x9a9df0, 0xe6dca0, 0xc0c0c0};
        return colors[static_cast< int >(s)] | ((alpha & 255) << 24);
    }
    std::uint32_t fieldColor(const Field& f, unsigned alpha) {
        // Row identity survives edits and reloads; unowned fields receive a scene-local key.
        std::uint64_t key = f.row.valid() ? f.row.v : f.colorKey;
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        const float hue = (key % 3600) / 600.0f;
        const float chroma = 0.65f, low = 0.30f;
        const float mid = chroma * (1 - std::fabs(std::fmod(hue, 2.0f) - 1));
        const float rgb[6][3] = {{chroma,mid,0},{mid,chroma,0},{0,chroma,mid},
                                  {0,mid,chroma},{mid,0,chroma},{chroma,0,mid}};
        const auto& c = rgb[static_cast<int>(hue)];
        return static_cast<unsigned>((c[0]+low)*255) | (static_cast<unsigned>((c[1]+low)*255)<<8) |
               (static_cast<unsigned>((c[2]+low)*255)<<16) | ((alpha & 255)<<24);
    }
    float rangeFade(float distance, float distant, float range) {
        if (range < 0) return 1;
        if (range == 0) return distance < distant ? 1 : 0;
        const float t = std::clamp((range + distant - distance) / (range * 0.25f), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }
    Mesh geometry(const Field& f, bool selected, bool hovered, bool recede) {
        Mesh m;
        unsigned c = fieldColor(f, selected ? 255 : hovered ? 240 : !f.active ? 35 : recede ? 65 : 135);
        float r = std::max(1.0f, f.radius);
        Vec3 n = unit(f.y), x, z;
        basis(n, x, z);
        switch (f.shape) {
        case Shape::Point:
            sphere(m, f.center, r, c, false, true);
            break;
        case Shape::Cube:
            box(m, f, c);
            break;
        case Shape::Parallel:
            if (f.bound == Bound::Box)
                box(m, f, c);
            else if (f.bound == Bound::Cylinder)
                tube(m, f.center, add(f.center, mul(n, f.height)), r, r, c);
            else
                sphere(m, f.center, f.range < 0 ? 500 : std::max(1.0f, f.range), c, false, true);
            break;
        case Shape::Cone:
            tube(m, f.center, add(f.center, mul(n, f.height * (1 - f.topCut))), r, r * f.topCut, c);
            break;
        case Shape::Disk:
        case Shape::Torus: {
            if (f.shape == Shape::Disk) {
                x = unit(f.x);
                z = unit(f.z);
            }
            ring(m, f.center, x, z, r, c, false, f.degrees);
            float inner = f.shape == Shape::Torus ? std::max(0.0f, f.innerRadius) : 0;
            if (inner > 0)
                ring(m, f.center, x, z, inner, c);
            for (int i = 0; i < 32; i++) {
                float a = f.degrees * pi / 180 * i / 32, b = f.degrees * pi / 180 * (i + 1) / 32;
                auto point = [&](float t, float rr) { return add(f.center, add(mul(x, rr * std::cos(t)), mul(z, rr * std::sin(t)))); };
                Vec3 p0 = point(a, inner), p1 = point(a, r), p2 = point(b, r), p3 = point(b, inner);
                m.triangles.push_back({p0, p1, p2, (c & 0xffffff) | 0x24000000});
                m.triangles.push_back({p0, p2, p3, (c & 0xffffff) | 0x24000000});
            }
            break;
        }
        case Shape::Segment:
        case Shape::Wire:
            for (size_t i = 1; i < f.points.size(); i++)
                tube(m, f.points[i - 1], f.points[i], std::max(8.0f, f.distant), std::max(8.0f, f.distant), c);
            break;
        case Shape::Unknown:
            break;
        }
        if (f.range >= 0 && f.shape != Shape::Parallel) {
            float reach = std::max(1.0f, f.range + f.distant);
            unsigned shell = (c & 0xffffff) | 0x65000000;
            if (f.shape == Shape::Point)
                sphere(m, f.center, reach, shell, true, false);
            else if (f.shape == Shape::Segment || f.shape == Shape::Wire) {
                for (size_t i = 1; i < f.points.size(); i++)
                    tube(m, f.points[i - 1], f.points[i], reach, reach, shell, false, true);
            } else if (f.shape == Shape::Disk || f.shape == Shape::Torus) {
                ring(m, add(f.center, mul(n, reach)), x, z, r, shell, true);
                ring(m, add(f.center, mul(n, -reach)), x, z, r, shell, true);
                ring(m, f.center, x, z, r + reach, shell, true);
            }
        }
        if (!selected && !hovered)
            m.triangles.clear();
        else
            for (auto& triangle : m.triangles)
                triangle.color = fieldColor(f, selected ? 9 : 6);
        for (auto& line : m.lines) {
            const bool shell = (line.color >> 24) == 0x65;
            line.color = fieldColor(f, shell ? (selected || hovered ? 85 : 25) : (c >> 24));
            line.width = selected ? 3.5f : hovered ? 2.5f : 1.5f;
        }
        return m;
    }
    Op numericOp(const ModelView& model, const std::vector< RowId >& rows, const char* column, float value, bool integer) {
        Op op;
        op.label = std::string("Set gravity ") + column;
        if (!std::isfinite(value))
            return op;
        if (std::strcmp(column, "Range") == 0)
            value = value < 0 ? -1 : value;
        if (std::strcmp(column, "Priority") == 0 || std::strcmp(column, "Distant") == 0)
            value = std::max(0.0f, value);
        for (RowId row : rows)
            if (shapeForPlacement(model.objectName(row)) != Shape::Unknown) {
                Cell cell;
                cell.type = integer ? CellType::Int : CellType::Float;
                cell.i = static_cast< int >(std::clamp(value, -10000000.0f, 10000000.0f));
                cell.f = value;
                if (const Cell* old = model.cell(row, columnHash(column)))
                    cell.type = old->type;
                op.sets.push_back({row, columnHash(column), cell});
            }
        return op;
    }
    Op shapeOp(const ModelView& model, RowId row, Shape shape, Bound bound) {
        Op op;
        op.label = "Change gravity shape";
        if (shape == Shape::Unknown || shapeForPlacement(model.objectName(row)) == Shape::Unknown)
            return op;
        if (shape == Shape::Wire) {
            const Cell* path = model.cell(row, columnHash("CommonPath_ID"));
            int zone = 0, table = 0;
            bool found = false;
            if (!path || path->i < 0 || !model.locate(row, &zone, &table))
                return op;
            for (int t = 0; t < model.tableCount(zone); t++) {
                std::string p = model.tablePath(zone, t);
                if (p.size() < 14 || p.substr(p.size() - 14) != "commonpathinfo")
                    continue;
                model.forEachRow(zone, t, [&](RowId r) {
                    const Cell* id = model.cell(r, columnHash("l_id"));
                    if (!id)
                        id = model.cell(r, columnHash("id"));
                    if (id && id->i == path->i && model.state(r) != RowState::Deleted && model.railPoints(r).size() >= 2)
                        found = true;
                });
            }
            if (!found)
                return op;
        }
        Cell name;
        name.type = CellType::StringOffset;
        name.s = placementName(shape, bound);
        if (const Cell* old = model.cell(row, columnHash("name")))
            name.type = old->type;
        op.sets.push_back({row, columnHash("name"), name});
        for (int i = 0; i < 3; i++) {
            Cell arg;
            arg.i = -1;
            if (shape == Shape::Wire && i == 0)
                arg.i = 20;
            op.sets.push_back({row, columnHash(("Obj_arg" + std::to_string(i)).c_str()), arg});
        }
        return op;
    }
    std::vector< Check > makeChecks(const View& v, const std::vector< RowId >& baselineGround) {
        std::vector< Check > out;
        if (v.registered >= 116) {
            Check c;
            c.key = "gravity-capacity";
            c.severity = v.registered >= 128 ? Severity::Error : Severity::Warning;
            c.message = "Gravity fields: " + std::to_string(v.registered) + " / 128 registered slots. Deleted fields keep their slot until Reload.";
            for (auto& f : v.fields)
                if (f.row.valid())
                    c.rows.push_back(f.row);
            out.push_back(std::move(c));
        }
        std::set< RowId > reported(baselineGround.begin(), baselineGround.end());
        for (auto& d : v.deadGround)
            if (reported.insert(d.row).second) {
                Check c;
                c.key = "gravity-ground:" + std::to_string(d.row.v);
                c.message = "Ground with no gravity at (" + std::to_string(static_cast< int >(d.position.x)) + ", " +
                            std::to_string(static_cast< int >(d.position.y)) + ", " + std::to_string(static_cast< int >(d.position.z)) +
                            "). Add a field or extend its range.";
                if (d.row.valid())
                    c.rows.push_back(d.row);
                out.push_back(std::move(c));
            }
        return out;
    }
    void setVisible(bool v) {
        shown = v;
    }
    bool visible() {
        return shown;
    }
    void setAirProbe(bool v) {
        air = v;
    }
    bool airProbe() {
        return air;
    }
    void publish(View v) {
        std::lock_guard< std::mutex > guard(lock);
        current = std::move(v);
    }
    View view() {
        std::lock_guard< std::mutex > guard(lock);
        return current;
    }
    void reset() {
        publish({});
    }
}  // namespace PetariEditor::Gravity
