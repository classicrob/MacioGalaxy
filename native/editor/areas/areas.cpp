#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <petari/editor/areas.hpp>
#include <petari/editor/catalog.hpp>
#include <set>

namespace PetariEditor::Areas {
    namespace {
        namespace D = DebugDraw;
        using namespace Gizmo;
        const Definition definitions[] = {
#include "definitions.inc"
        };
        Visibility areaVisibility = Visibility::Selection, cameraVisibility = Visibility::Selection;
        std::map< RowId, RowTransform > previews;
        int integer(const ModelView& m, RowId row, const char* field, int fallback = -1) {
            const auto* cell = m.cell(row, columnHash(field));
            return cell ? cell->i : fallback;
        }
        std::string text(const ModelView& m, RowId row, const char* field) {
            const auto* cell = m.cell(row, columnHash(field));
            return cell ? cell->s : "";
        }
        V3 local(const Volume& v, Vec3 p) {
            const auto d = D::vector(p) - D::vector(v.origin);
            return {dot(d, v.axes.x), dot(d, v.axes.y), dot(d, v.axes.z)};
        }
        Vec3 world(const Volume& v, V3 p) {
            return D::point(D::vector(v.origin) + v.axes.x * p.x + v.axes.y * p.y + v.axes.z * p.z);
        }
        // Support points for the game's convex forms; bowl is the lower half of a sphere.
        V3 support(const Volume& v, V3 direction) {
            const V3 d{dot(direction, v.axes.x), dot(direction, v.axes.y), dot(direction, v.axes.z)};
            V3 p;
            const auto shape = v.definition->shape;
            if (shape == Shape::Box || shape == Shape::BaseBox) {
                p = {std::copysign(v.size.x, d.x), std::copysign(v.size.y, d.y), std::copysign(v.size.z, d.z)};
                if (shape == Shape::BaseBox)
                    p.y += v.size.y;
            } else if (shape == Shape::Cylinder) {
                const auto radial = normalized(V3{d.x, 0, d.z}, {1, 0, 0}) * v.size.x;
                p = {radial.x, d.y > 0 ? v.size.y : 0, radial.z};
            } else {
                p = normalized(d, {1, 0, 0}) * v.size.x;
                if (shape == Shape::Bowl && p.y > 0)
                    p = normalized(V3{d.x, 0, d.z}, {1, 0, 0}) * v.size.x;
            }
            return D::vector(world(v, p));
        }
        bool simplex(std::vector< V3 >& s, V3& d) {
            const V3 a = s.back(), ao = a * -1;
            if (s.size() == 2) {
                const V3 ab = s[0] - a;
                if (dot(ab, ao) > 0)
                    d = cross(cross(ab, ao), ab);
                else {
                    s = {a};
                    d = ao;
                }
            } else if (s.size() == 3) {
                const V3 b = s[1], c = s[0], ab = b - a, ac = c - a, abc = cross(ab, ac);
                if (dot(cross(abc, ac), ao) > 0) {
                    if (dot(ac, ao) > 0) {
                        s = {c, a};
                        d = cross(cross(ac, ao), ac);
                    } else {
                        s = {b, a};
                        return simplex(s, d);
                    }
                } else if (dot(cross(ab, abc), ao) > 0) {
                    s = {b, a};
                    return simplex(s, d);
                } else if (dot(abc, ao) > 0)
                    d = abc;
                else {
                    s = {b, c, a};
                    d = abc * -1;
                }
            } else {
                const V3 b = s[2], c = s[1], e = s[0];
                const std::array< std::array< V3, 3 >, 3 > faces{{{{b, c, e}}, {{c, e, b}}, {{e, b, c}}}};
                for (const auto& f : faces) {
                    V3 n = cross(f[0] - a, f[1] - a);
                    if (dot(n, f[2] - a) > 0)
                        n = n * -1;
                    if (dot(n, ao) > 0) {
                        s = {f[1], f[0], a};
                        return simplex(s, d);
                    }
                }
                return true;
            }
            return dot(d, d) < 1e-10f;
        }
    }  // namespace
    const Definition* definition(const char* name) {
        if (name)
            for (const auto& d : definitions)
                if (std::strcmp(d.name, name) == 0)
                    return &d;
        return nullptr;
    }
    const char* shapeName(Shape s) {
        const char* names[] = {"Box (centred)", "Box (base origin)", "Sphere", "Bowl", "Cylinder"};
        return names[static_cast< int >(s)];
    }
    const char* kindName(Kind k) {
        const char* names[] = {"Camera", "Death", "Water", "Switch", "Message", "Area"};
        return names[static_cast< int >(k)];
    }
    std::uint32_t color(Kind k, unsigned alpha) {
        const std::uint32_t palette[] = {0x00ffb26e, 0x006060ff, 0x00ffd34b, 0x0072e889, 0x00ff88df, 0x0066d8ff};
        return palette[static_cast< int >(k)] | (std::min(alpha, 255u) << 24);
    }
    bool isAreaTable(const char* path) {
        if (!path)
            return false;
        const std::string p(path);
        return p.find("/areaobjinfo") != std::string::npos || p.find("/cameracubeinfo") != std::string::npos;
    }
    std::optional< Volume > describe(const ModelView& m, RowId row, const RowTransform* previewTransform) {
        int zone, table;
        if (!m.locate(row, &zone, &table) || m.state(row) == RowState::Deleted || !isAreaTable(m.tablePath(zone, table)))
            return {};
        const auto* spec = definition(m.objectName(row));
        if (!spec || !m.hasTransform(row))
            return {};
        const auto t = previewTransform ? *previewTransform : m.transform(row);
        const auto rotation = fromEulerDegrees(D::vector(t.dirDeg));
        const auto matrix = m.zoneWorldMatrix(zone);
        Volume v;
        v.row = row;
        v.zone = zone;
        v.definition = spec;
        v.missions = m.rowMissions(row);
        v.origin = D::transform(matrix, t.pos);
        auto axis = [&](V3 p) { return D::vector(D::transform(matrix, D::point(p))) - D::vector(D::transform(matrix, {})); };
        v.axes = {axis(rotation.x), axis(rotation.y), axis(rotation.z)};
        v.size = {500 * t.scale.x, 500 * t.scale.y, 500 * t.scale.z};
        v.priority = integer(m, row, "Obj_arg2");
        v.cameraId = integer(m, row, "Obj_arg0");
        const int category = std::max(0, integer(m, row, "Obj_arg3"));
        v.categories = category >> 4 ? category >> 4 : 1u << (category & 15);
        v.valid = std::isfinite(v.origin.x) && std::isfinite(v.origin.y) && std::isfinite(v.origin.z) && std::isfinite(t.dirDeg.x) &&
                  std::isfinite(t.dirDeg.y) && std::isfinite(t.dirDeg.z) && v.size.x > 0 && std::isfinite(v.size.x) &&
                  ((spec->shape == Shape::Sphere || spec->shape == Shape::Bowl) || (v.size.y > 0 && std::isfinite(v.size.y))) &&
                  ((spec->shape != Shape::Box && spec->shape != Shape::BaseBox) || (v.size.z > 0 && std::isfinite(v.size.z)));
        return v;
    }
    std::vector< Volume > collect(const ModelView& m) {
        std::vector< Volume > out;
        for (int z = 0; z < m.zoneCount(); ++z)
            for (int t = 0; t < m.tableCount(z); ++t)
                if (isAreaTable(m.tablePath(z, t)))
                    m.forEachRow(z, t, [&](RowId row) {
                        if (auto v = describe(m, row))
                            out.push_back(*v);
                    });
        return out;
    }
    Vec3 center(const Volume& v) {
        const auto s = v.definition->shape;
        return world(v, {0, s == Shape::BaseBox ? v.size.y : s == Shape::Cylinder ? v.size.y / 2 : s == Shape::Bowl ? -v.size.x / 2 : 0, 0});
    }
    float radius(const Volume& v) {
        if (v.definition->shape == Shape::Box || v.definition->shape == Shape::BaseBox)
            return length(D::vector(v.size));
        if (v.definition->shape == Shape::Cylinder)
            return std::hypot(v.size.x, v.size.y / 2);
        return v.definition->shape == Shape::Bowl ? v.size.x * std::sqrt(1.25f) : v.size.x;
    }
    bool contains(const Volume& v, Vec3 point) {
        if (!v.valid)
            return false;
        const auto p = local(v, point);
        switch (v.definition->shape) {
        case Shape::Box:
            return std::fabs(p.x) < v.size.x && std::fabs(p.y) < v.size.y && std::fabs(p.z) < v.size.z;
        case Shape::BaseBox:
            return std::fabs(p.x) < v.size.x && p.y >= 0 && p.y < v.size.y * 2 && std::fabs(p.z) < v.size.z;
        case Shape::Sphere:
            return dot(p, p) < v.size.x * v.size.x;
        case Shape::Bowl:
            return p.y < 0 && dot(p, p) <= v.size.x * v.size.x;
        case Shape::Cylinder:
            return p.y >= 0 && p.y <= v.size.y && p.x * p.x + p.z * p.z < v.size.x * v.size.x;
        }
        return false;
    }
    bool overlaps(const Volume& a, const Volume& b) {
        if (!a.valid || !b.valid)
            return false;
        V3 d = D::vector(center(b)) - D::vector(center(a));
        if (length(d) > radius(a) + radius(b))
            return false;
        if (dot(d, d) < 1e-8f)
            d = {1, 0, 0};
        auto minkowski = [&](V3 v) { return support(a, v) - support(b, v * -1); };
        std::vector< V3 > points{minkowski(d)};
        d = points[0] * -1;
        for (int i = 0; i < 64; ++i) {
            if (dot(d, d) < 1e-8f)
                return true;
            const auto next = minkowski(d);
            if (dot(next, d) < 0)
                return false;
            points.push_back(next);
            if (simplex(points, d))
                return true;
        }
        return false;
    }
    DebugDraw::Mesh geometry(const Volume& v, bool selected) {
        D::Mesh mesh;
        if (!v.valid)
            return mesh;
        const auto line = color(v.definition->kind, selected ? 255 : 170), fill = color(v.definition->kind, selected ? 42 : 20);
        auto edge = [&](V3 a, V3 b) { mesh.lines.push_back({world(v, a), world(v, b), line, selected ? 3.0f : 1.5f}); };
        auto tri = [&](V3 a, V3 b, V3 c) { mesh.triangles.push_back({world(v, a), world(v, b), world(v, c), fill}); };
        const auto shape = v.definition->shape;
        if (shape == Shape::Box || shape == Shape::BaseBox) {
            std::array< V3, 8 > p;
            for (int i = 0; i < 8; ++i) {
                p[i] = {(i & 1 ? 1.f : -1.f) * v.size.x, (i & 2 ? 1.f : -1.f) * v.size.y, (i & 4 ? 1.f : -1.f) * v.size.z};
                if (shape == Shape::BaseBox)
                    p[i].y += v.size.y;
            }
            for (int i = 0; i < 8; ++i)
                for (int bit : {1, 2, 4})
                    if (!(i & bit))
                        edge(p[i], p[i | bit]);
            for (const auto& f : std::array< std::array< int, 4 >, 6 >{
                     {{{0, 1, 3, 2}}, {{4, 6, 7, 5}}, {{0, 4, 5, 1}}, {{2, 3, 7, 6}}, {{0, 2, 6, 4}}, {{1, 5, 7, 3}}}}) {
                tri(p[f[0]], p[f[1]], p[f[2]]);
                tri(p[f[0]], p[f[2]], p[f[3]]);
            }
        } else if (shape == Shape::Cylinder) {
            for (int i = 0; i < 32; ++i) {
                const float a = 2 * kPi * i / 32, b = 2 * kPi * (i + 1) / 32;
                const V3 p{v.size.x * std::cos(a), 0, v.size.x * std::sin(a)}, q{v.size.x * std::cos(b), 0, v.size.x * std::sin(b)},
                    up{0, v.size.y, 0};
                edge(p, q);
                edge(p + up, q + up);
                if (i % 4 == 0)
                    edge(p, p + up);
                tri(p, q, q + up);
                tri(p, q + up, p + up);
                tri({}, q, p);
                tri(up, p + up, q + up);
            }
        } else {
            const int rings = shape == Shape::Bowl ? 6 : 12;
            auto sphere = [&](int j, int i) {
                const float latitude = -kPi / 2 + kPi * j / 12, longitude = 2 * kPi * i / 24;
                return V3{v.size.x * std::cos(latitude) * std::cos(longitude), v.size.x * std::sin(latitude),
                          v.size.x * std::cos(latitude) * std::sin(longitude)};
            };
            for (int j = 0; j < rings; ++j)
                for (int i = 0; i < 24; ++i) {
                    auto a = sphere(j, i), b = sphere(j, i + 1), c = sphere(j + 1, i + 1), d = sphere(j + 1, i);
                    tri(a, b, c);
                    tri(a, c, d);
                    if (j % 3 == 0 || j == rings - 1)
                        edge(d, c);
                    if (i % 4 == 0)
                        edge(a, d);
                    if (shape == Shape::Bowl && j == rings - 1)
                        tri({}, d, c);
                }
        }
        return mesh;
    }
    RowId cameraRow(const ModelView& m, const Volume& area) {
        if (area.definition->kind != Kind::Camera || area.cameraId < 0)
            return {};
        char id[16];
        std::snprintf(id, sizeof(id), "c:%04x", area.cameraId);
        RowId result;
        for (int t = 0; t < m.tableCount(area.zone); ++t)
            if (std::string(m.tablePath(area.zone, t)).find("cameraparam.bcam") != std::string::npos)
                m.forEachRow(area.zone, t, [&](RowId row) {
                    if (m.state(row) != RowState::Deleted && text(m, row, "id") == id)
                        result = row;
                });
        return result;
    }
    void computeChecks(const ModelView& m, std::vector< Check >& out) {
        const auto volumes = collect(m);
        std::vector< RowId > starts;
        for (int z = 0; z < m.zoneCount(); ++z)
            for (int t = 0; t < m.tableCount(z); ++t) {
                const std::string path(m.tablePath(z, t));
                if (path.find("/startinfo") != std::string::npos)
                    m.forEachRow(z, t, [&](RowId row) {
                        if (m.state(row) != RowState::Deleted)
                            starts.push_back(row);
                    });
            }
        for (const auto& v : volumes) {
            if (!v.valid)
                out.push_back(
                    {"area-size:" + std::to_string(v.row.v), Severity::Error, "Area dimensions must be positive and finite", {v.row}, "", {}, ""});
            if (v.definition->kind == Kind::Camera && v.cameraId >= 0 && !cameraRow(m, v).valid())
                out.push_back({"area-camera-missing:" + std::to_string(v.row.v),
                               Severity::Warning,
                               "Camera area has no matching camera parameters",
                               {v.row},
                               "",
                               {},
                               ""});
        }
        for (int mission = 1; mission <= std::min(m.missionCount(), 32); ++mission) {
            const unsigned mask = 1u << (mission - 1);
            std::map< std::string, std::vector< RowId > > counts;
            std::map< std::string, int > caps;
            for (std::size_t i = 0; i < volumes.size(); ++i) {
                const auto& a = volumes[i];
                if (!(a.missions & mask))
                    continue;
                counts[a.definition->manager].push_back(a.row);
                caps[a.definition->manager] = a.definition->capacity;
                if (a.definition->kind == Kind::Camera && text(m, a.row, "Validity") != "Invalid") {
                    for (std::size_t j = i + 1; j < volumes.size(); ++j) {
                        const auto& b = volumes[j];
                        if (b.definition->kind != Kind::Camera || !(b.missions & mask) || !(a.categories & b.categories) ||
                            a.priority != b.priority || text(m, b.row, "Validity") == "Invalid" || !overlaps(a, b))
                            continue;
                        const auto lo = std::min(a.row.v, b.row.v), hi = std::max(a.row.v, b.row.v);
                        out.push_back({"area-camera-overlap:" + std::to_string(lo) + ":" + std::to_string(hi) + ":" + std::to_string(mission),
                                       Severity::Warning,
                                       "Overlapping camera areas have the same priority in mission " + std::to_string(mission),
                                       {a.row, b.row},
                                       "",
                                       {},
                                       ""});
                    }
                }
                if (a.definition->kind == Kind::Death && (std::strcmp(a.definition->manager, "DeathArea") != 0 || integer(m, a.row, "Obj_arg0") <= 0))
                    for (RowId start : starts) {
                        if (!(m.rowMissions(start) & mask) || !m.hasTransform(start))
                            continue;
                        int z = 0;
                        m.locate(start, &z, nullptr);
                        const auto matrix = m.zoneWorldMatrix(z);
                        const auto t = m.transform(start);
                        const auto p = D::transform(matrix, t.pos);
                        const auto up = fromEulerDegrees(D::vector(t.dirDeg)).y;
                        const auto below = D::transform(matrix, D::point(D::vector(t.pos) - up * 100));
                        if (contains(a, p) || contains(a, below))
                            out.push_back(
                                {"area-death-start:" + std::to_string(a.row.v) + ":" + std::to_string(start.v) + ":" + std::to_string(mission),
                                 Severity::Warning,
                                 "A death area touches a start or the 100 units directly below it in mission " + std::to_string(mission),
                                 {a.row, start},
                                 "",
                                 {},
                                 ""});
                    }
            }
            std::set< std::pair< int, int > > cameraIds;
            for (const auto& v : volumes)
                if ((v.missions & mask) && v.definition->kind == Kind::Camera && v.cameraId >= 0)
                    cameraIds.emplace(v.zone, v.cameraId);
            if (cameraIds.size() > 1024)
                out.push_back({"area-camera-capacity:" + std::to_string(mission),
                               Severity::Error,
                               "Camera areas alone require more than the 1,024 camera chunk slots in mission " + std::to_string(mission),
                               {},
                               "",
                               {},
                               ""});
            for (const auto& [manager, rows] : counts)
                if (rows.size() > static_cast< std::size_t >(caps[manager]))
                    out.push_back({"area-capacity:" + manager + ":" + std::to_string(mission),
                                   Severity::Error,
                                   manager + " uses " + std::to_string(rows.size()) + " areas; the limit is " + std::to_string(caps[manager]) +
                                       " in mission " + std::to_string(mission),
                                   rows,
                                   "",
                                   {},
                                   ""});
        }
    }
    void registerCatalog() {
        using namespace Catalog;
        for (const auto& spec : definitions) {
            const auto* existing = find(spec.name);
            if (!existing)
                continue;
            ClassInfo info = *existing;
            auto arg = [&](int index, const char* name, const char* help, std::vector< EnumChoice > choices = {}) {
                info.args.erase(std::remove_if(info.args.begin(), info.args.end(), [&](const Arg& a) { return a.index == index; }), info.args.end());
                info.args.push_back({index, name, choices.empty() ? ArgType::Int : ArgType::Enum, -1, std::move(choices), "", help});
            };
            if (spec.kind == Kind::Camera) {
                arg(0, "Camera", "ID of the camera parameters used by this area (Obj_arg0)");
                arg(2, "Priority", "Higher priority wins where camera areas overlap (Obj_arg2)");
                arg(3, "Player mode", "Player category, or a category bit mask shifted left by 4 (Obj_arg3)",
                    {{-1, "Normal"}, {0, "Normal"}, {1, "Swimming"}, {2, "Water surface"}, {3, "Pull Star"}, {4, "Flying"}});
            } else if (std::strcmp(spec.manager, "Water") == 0) {
                arg(1, "Temperature", "Cold water periodically damages Mario while swimming (Obj_arg1)", {{-1, "Normal"}, {1, "Cold"}});
            } else if (std::strcmp(spec.manager, "MessageArea") == 0) {
                arg(0, "Message ID", "Message identifier whose talk trigger is restricted to this area (Obj_arg0)");
            } else if (std::strcmp(spec.manager, "DeathArea") == 0) {
                arg(0, "Affects Mario", "Whether entering this volume kills Mario (Obj_arg0)", {{-1, "Yes"}, {0, "Yes"}, {1, "No"}});
            } else if (std::strcmp(spec.name, "SwitchCube") == 0 || std::strcmp(spec.name, "SwitchSphere") == 0 ||
                       std::strcmp(spec.name, "SwitchCylinder") == 0) {
                arg(0, "On exit", "With a value other than -1, leaving turns switch A off (Obj_arg0)",
                    {{-1, "Keep switch state"}, {0, "Turn A off"}});
                arg(1, "On enter", "Turns switch A on for -1, off for other values (Obj_arg1)", {{-1, "Turn A on"}, {0, "Turn A off"}});
                arg(2, "Trigger while", "With a value other than -1, Mario must stand on ground (Obj_arg2)",
                    {{-1, "Anywhere inside"}, {0, "On ground"}});
            }
            addClass(info);
        }
    }
    void setVisibility(bool cameras, Visibility v) {
        (cameras ? cameraVisibility : areaVisibility) = v;
    }
    Visibility visibility(bool cameras) {
        return cameras ? cameraVisibility : areaVisibility;
    }
    bool shown(const Volume& v, const std::vector< RowId >& selection) {
        const auto mode = visibility(v.definition->kind == Kind::Camera);
        return mode == Visibility::All || (mode == Visibility::Selection && std::find(selection.begin(), selection.end(), v.row) != selection.end());
    }
    void setPreview(RowId row, const RowTransform& t) {
        previews[row] = t;
    }
    void clearPreview(RowId row) {
        previews.erase(row);
    }
    void clearPreviews() {
        previews.clear();
    }
    const RowTransform* preview(RowId row) {
        auto it = previews.find(row);
        return it == previews.end() ? nullptr : &it->second;
    }
}  // namespace PetariEditor::Areas
