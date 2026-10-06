// The headless world (world.hpp): colliders from the placed rows' .kcl files and terrain brushes, ray and
// nearest-surface queries, approximate gravity.
#include "world.hpp"

#include "model_io.hpp"

#include <petari/editor/terrain/brush.hpp>
#include <petari/editor/terrain/mesh.hpp>
#include <petari/editor/terrain/kcl_reader.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace PetariEditor::LevelApi {

namespace {
    // A row's world transform with scale: world = zone * T * R * S.
    struct Affine {
        G::V3 origin;
        G::V3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};  // columns, scaled
        G::V3 apply(G::V3 p) const { return origin + x * p.x + y * p.y + z * p.z; }
    };

    Affine rowAffine(const ModelView& model, RowId row, int zone) {
        const RowTransform t = model.transform(row);
        const Frame f = zoneFrame(model, zone);
        const G::Basis b = basisToWorld(f, G::fromEulerDegrees(v3(t.dirDeg)));
        Affine a;
        a.origin = toWorld(f, v3(t.pos));
        a.x = b.x * t.scale.x;
        a.y = b.y * t.scale.y;
        a.z = b.z * t.scale.z;
        return a;
    }

    bool rayBox(G::V3 o, G::V3 d, G::V3 lo, G::V3 hi, float maxT) {
        float t0 = 0, t1 = maxT;
        const float os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z}, ls[3] = {lo.x, lo.y, lo.z}, hs[3] = {hi.x, hi.y, hi.z};
        for (int i = 0; i < 3; i++) {
            if (std::fabs(ds[i]) < 1e-9f) {
                if (os[i] < ls[i] || os[i] > hs[i]) return false;
                continue;
            }
            float a = (ls[i] - os[i]) / ds[i], b = (hs[i] - os[i]) / ds[i];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
            if (t0 > t1) return false;
        }
        return true;
    }

    // Möller-Trumbore, both faces.
    bool rayTriangle(G::V3 o, G::V3 d, const Triangle& t, float* pT) {
        const G::V3 e1 = t.b - t.a, e2 = t.c - t.a;
        const G::V3 p = G::cross(d, e2);
        const float det = G::dot(e1, p);
        if (std::fabs(det) < 1e-8f) return false;
        const float inv = 1.0f / det;
        const G::V3 s = o - t.a;
        const float u = G::dot(s, p) * inv;
        if (u < 0 || u > 1) return false;
        const G::V3 q = G::cross(s, e1);
        const float v = G::dot(d, q) * inv;
        if (v < 0 || u + v > 1) return false;
        const float hit = G::dot(e2, q) * inv;
        if (hit < 0) return false;
        *pT = hit;
        return true;
    }

    G::V3 closestOnTriangle(G::V3 p, const Triangle& t) {
        // Ericson, Real-Time Collision Detection 5.1.5.
        const G::V3 ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
        const float d1 = G::dot(ab, ap), d2 = G::dot(ac, ap);
        if (d1 <= 0 && d2 <= 0) return t.a;
        const G::V3 bp = p - t.b;
        const float d3 = G::dot(ab, bp), d4 = G::dot(ac, bp);
        if (d3 >= 0 && d4 <= d3) return t.b;
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0 && d1 >= 0 && d3 <= 0) return t.a + ab * (d1 / (d1 - d3));
        const G::V3 cp = p - t.c;
        const float d5 = G::dot(ab, cp), d6 = G::dot(ac, cp);
        if (d6 >= 0 && d5 <= d6) return t.c;
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0 && d2 >= 0 && d6 <= 0) return t.a + ac * (d2 / (d2 - d6));
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        const float denom = 1.0f / (va + vb + vc);
        return t.a + ab * (vb * denom) + ac * (vc * denom);
    }

    float boxDistance(G::V3 p, G::V3 lo, G::V3 hi) {
        const G::V3 d{std::max({lo.x - p.x, 0.0f, p.x - hi.x}), std::max({lo.y - p.y, 0.0f, p.y - hi.y}), std::max({lo.z - p.z, 0.0f, p.z - hi.z})};
        return G::length(d);
    }

    std::int32_t intCell(const ModelView& model, RowId row, const char* column, std::int32_t fallback) {
        const Cell* c = model.cell(row, columnHash(column));
        return c != nullptr && c->type != CellType::Float ? c->i : fallback;
    }

    float floatCell(const ModelView& model, RowId row, const char* column, float fallback) {
        const Cell* c = model.cell(row, columnHash(column));
        if (c == nullptr) return fallback;
        return c->type == CellType::Float ? c->f : static_cast< float >(c->i);
    }
}  // namespace

std::vector< Triangle > readKclTriangles(const Formats::Bytes& kcl) {
    std::vector<Triangle> out;
    const auto mesh = Terrain::readKcl(kcl);
    if (!mesh) return out;
    auto vec = [](Terrain::Vec3 p) { return G::V3{p.x, p.y, p.z}; };
    for (std::size_t i = 0; i < mesh->indices.size(); i += 3) {
        const auto index = mesh->indices[i];
        const G::V3 a = vec(mesh->positions[index]), b = vec(mesh->positions[mesh->indices[i + 1]]),
                    c = vec(mesh->positions[mesh->indices[i + 2]]);
        const G::V3 normal = G::cross(b - a, c - a);
        if (G::length(normal) < 1e-8f) continue;
        out.push_back({a, b, c, G::normalized(normal)});
    }
    return out;
}

void World::sync(const EditModel& model) {
    // The collision rows and their transforms: rebuild only when that changes.
    const int mission = model.scenario();
    struct Item {
        RowId row;
        int zone;
        std::string name;
    };
    std::vector< Item > items;
    std::ostringstream signature;
    for (int z = 0; z < model.zoneCount(); z++) {
        if (!model.zonePlaced(z)) continue;
        for (int t = 0; t < model.tableCount(z); t++) {
            model.forEachRow(z, t, [&](RowId row) {
                if (model.state(row) == RowState::Deleted || !model.hasTransform(row) || !(model.rowMissions(row) & (1u << (mission - 1)))) return;
                const char* pName = model.objectName(row);
                if (pName == nullptr || pName[0] == '\0') return;
                const Catalog::ClassInfo* info = Catalog::find(pName);
                // Collision comes from planets, map parts and anything the catalog says collides; brushes too.
                const bool candidate = info == nullptr || info->hasCollision || info->category == Catalog::Category::Planet ||
                                       info->category == Catalog::Category::Platform || std::strcmp(pName, "EditorBrush") == 0;
                if (!candidate) return;
                const RowTransform tr = model.transform(row);
                items.push_back({row, z, pName});
                signature << row.v << ' ' << pName << ' ' << tr.pos.x << ',' << tr.pos.y << ',' << tr.pos.z << ' ' << tr.dirDeg.x << ',' << tr.dirDeg.y << ','
                          << tr.dirDeg.z << ' ' << tr.scale.x << ',' << tr.scale.y << ',' << tr.scale.z << ' ' << intCell(model, row, "Obj_arg0", -1) << ';';
            });
        }
    }
    const std::string sig = signature.str();
    if (sig == mSignature) return;
    mSignature = sig;
    mColliders.clear();
    mSkipped.clear();
    for (const Item& item : items) {
        std::shared_ptr< const std::vector< Triangle > > local;
        if (item.name == "EditorBrush") {
            // Terrain brushes: the terrain library's own mesh for the row (live parts use the same).
            const RowTransform tr = model.transform(item.row);
            const Terrain::Brush brush =
                Terrain::brushFromRow(intCell(model, item.row, "Obj_arg0", 0), intCell(model, item.row, "Obj_arg1", 0), intCell(model, item.row, "Obj_arg2", 0),
                                      intCell(model, item.row, "Obj_arg3", 1), {tr.scale.x, tr.scale.y, tr.scale.z});
            const Terrain::Mesh mesh = Terrain::brushMesh(brush);
            auto tris = std::make_shared< std::vector< Triangle > >();
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                auto p = [&](std::uint32_t k) {
                    const auto& v = mesh.positions[mesh.indices[i + k]];
                    return G::V3{v.x, v.y, v.z};
                };
                const G::V3 a = p(0), b = p(1), c = p(2);
                tris->push_back({a, b, c, G::normalized(G::cross(b - a, c - a))});  // brush meshes wind outward
            }
            local = tris;
        } else {
            auto it = mCache.find(item.name);
            if (it == mCache.end()) {
                std::shared_ptr< std::vector< Triangle > > tris;
                if (std::optional< Formats::Bytes > raw = mSource("/ObjectData/" + item.name + ".arc")) {
                    if (std::optional< Formats::Archive > archive = Formats::Archive::parse(*raw)) {
                        const Formats::Bytes* kcl = archive->file(item.name + ".kcl");
                        if (kcl == nullptr) {
                            for (const std::string& f : archive->files()) {
                                if (kcl == nullptr && f.size() > 4 && f.compare(f.size() - 4, 4, ".kcl") == 0) kcl = archive->file(f);
                            }
                        }
                        if (kcl != nullptr) tris = std::make_shared< std::vector< Triangle > >(readKclTriangles(*kcl));
                        if (kcl == nullptr) mSkipped.push_back(item.name);
                    }
                }
                it = mCache.emplace(item.name, tris).first;
            }
            local = it->second;
        }
        if (!local || local->empty()) continue;
        // Brushes are sized by their mesh (scale is in it); other models by the row's scale.
        int zone = item.zone;
        Affine a = rowAffine(model, item.row, zone);
        if (item.name == "EditorBrush") {
            const RowTransform tr = model.transform(item.row);
            a.x = a.x * (tr.scale.x != 0 ? 1.0f / tr.scale.x : 1.0f);
            a.y = a.y * (tr.scale.y != 0 ? 1.0f / tr.scale.y : 1.0f);
            a.z = a.z * (tr.scale.z != 0 ? 1.0f / tr.scale.z : 1.0f);
        }
        Collider c;
        c.row = item.row;
        c.name = item.name;
        c.lo = {1e30f, 1e30f, 1e30f};
        c.hi = {-1e30f, -1e30f, -1e30f};
        c.tris.reserve(local->size());
        for (const Triangle& t : *local) {
            Triangle w{a.apply(t.a), a.apply(t.b), a.apply(t.c), {}};
            // The normal through the (possibly non-uniform) scale: the transformed edges' cross, on the local normal's side.
            const G::V3 local = a.x * t.n.x + a.y * t.n.y + a.z * t.n.z;
            w.n = G::normalized(G::cross(w.b - w.a, w.c - w.a));
            if (G::dot(w.n, local) < 0) w.n = w.n * -1.0f;
            for (const G::V3& p : {w.a, w.b, w.c}) {
                c.lo = {std::min(c.lo.x, p.x), std::min(c.lo.y, p.y), std::min(c.lo.z, p.z)};
                c.hi = {std::max(c.hi.x, p.x), std::max(c.hi.y, p.y), std::max(c.hi.z, p.z)};
            }
            c.tris.push_back(w);
        }
        mColliders.push_back(std::move(c));
    }
}

std::optional< Hit > World::ray(G::V3 origin, G::V3 dir, float maxDistance, bool frontOnly) const {
    std::optional< Hit > best;
    float bestT = maxDistance;
    for (const Collider& c : mColliders) {
        if (!rayBox(origin, dir, c.lo, c.hi, bestT)) continue;
        for (const Triangle& t : c.tris) {
            float hit;
            if (frontOnly && G::dot(t.n, dir) >= 0) continue;  // a back face: the inside of a planet
            if (rayTriangle(origin, dir, t, &hit) && hit < bestT) {
                bestT = hit;
                best = Hit{hit, origin + dir * hit, t.n, c.row};
            }
        }
    }
    return best;
}

std::optional< Hit > World::insideOf(G::V3 point, G::V3 down) const {
    // Inside a solid both nearest surfaces, above and below, are seen from behind. Collision meshes are not
    // closed (a canopy or a one-sided platform has only its top), so one back face overhead is not enough.
    const G::V3 up = down * -1.0f;
    std::optional< Hit > above = ray(point, up, 50000.0f, false);
    if (!above || G::dot(above->normal, up) <= 0.0f) return std::nullopt;
    std::optional< Hit > below = ray(point, down, 50000.0f, false);
    if (!below || G::dot(below->normal, down) <= 0.0f) return std::nullopt;
    return above;
}

std::optional< Hit > World::nearest(G::V3 point, float maxDistance) const {
    std::optional< Hit > best;
    float bestD = maxDistance;
    for (const Collider& c : mColliders) {
        if (boxDistance(point, c.lo, c.hi) > bestD) continue;
        for (const Triangle& t : c.tris) {
            const G::V3 q = closestOnTriangle(point, t);
            const float d = G::length(q - point);
            if (d < bestD) {
                bestD = d;
                best = Hit{d, q, t.n, c.row};
            }
        }
    }
    return best;
}

Gravity World::gravityAt(const EditModel& model, G::V3 point) const {
    // The gravity rows in this mission that cover the point, with the shapes GravityCreator.cpp builds from
    // a row (500 units per unit of scale): highest Priority wins, then the nearest (the engine blends equal
    // priorities). Cones, tori and wires are taken as their centre. Live mode's gravity is the engine's own.
    Gravity best;
    int bestPriority = -1000000;
    float bestDistance = 1e30f;
    const int mission = model.scenario();
    for (int z = 0; z < model.zoneCount(); z++) {
        if (!model.zonePlaced(z)) continue;
        for (int t = 0; t < model.tableCount(z); t++) {
            model.forEachRow(z, t, [&](RowId row) {
                if (model.state(row) == RowState::Deleted || !model.hasTransform(row) || !(model.rowMissions(row) & (1u << (mission - 1)))) return;
                const char* pName = model.objectName(row);
                if (pName == nullptr) return;
                const std::string name = pName;
                if (name.find("Gravity") == std::string::npos || name.rfind("Zero", 0) == 0) return;
                const RowTransform tr = model.transform(row);
                const Frame f = zoneFrame(model, z);
                const G::Basis axes = basisToWorld(f, G::fromEulerDegrees(v3(tr.dirDeg)));
                const G::V3 c = toWorld(f, v3(tr.pos));
                const G::V3 s{tr.scale.x * 500.0f, tr.scale.y * 500.0f, tr.scale.z * 500.0f};
                const G::V3 up = axes.y;
                const float range = floatCell(model, row, "Range", -1.0f);
                // Fields below 0 keep the class default (GravityUtil.cpp getJMapInfoArgPlus).
                const float distantField = floatCell(model, row, "Distant", -1.0f);
                float distant = std::max(0.0f, distantField);
                auto inRange = [&](float d) { return range < 0 || d < range + distant; };
                auto boxNearest = [&](G::V3 center) {
                    G::V3 q = center;
                    const G::V3 d = point - center;
                    q = q + axes.x * std::clamp(G::dot(d, axes.x), -s.x, s.x) + axes.y * std::clamp(G::dot(d, axes.y), -s.y, s.y) +
                        axes.z * std::clamp(G::dot(d, axes.z), -s.z, s.z);
                    return q;
                };
                G::V3 toward;
                float distance = 0;
                if (name == "GlobalPlaneGravity") {
                    if (range >= 0 && G::length(c - point) >= range) return;
                    toward = up * -1.0f;
                } else if (name == "GlobalPlaneGravityInBox") {
                    const G::V3 center = c + up * s.y;
                    if (G::length(boxNearest(center) - point) > 1e-3f) return;
                    toward = up * -1.0f;
                } else if (name == "GlobalPlaneGravityInCylinder") {
                    const float height = G::dot(point - c, up);
                    const G::V3 flat = (point - c) - up * height;
                    if (height < 0 || height > s.y || G::length(flat) > s.x) return;
                    toward = up * -1.0f;
                    distance = G::length(flat);
                } else if (name == "GlobalCubeGravity") {
                    const G::V3 q = boxNearest(c + up * s.y);
                    toward = q - point;
                    distance = G::length(toward);
                    if (distance < 1e-3f) toward = (c + up * s.y) - point;
                    if (!inRange(distance)) return;
                } else if (name == "GlobalDiskGravity" || name == "GlobalDiskTorusGravity") {
                    const float radius = 500.0f * std::max({tr.scale.x, tr.scale.y, tr.scale.z});
                    const float height = G::dot(point - c, up);
                    const bool bothSides = intCell(model, row, "Obj_arg0", -1) != 0;
                    if (!bothSides && height < 0) return;
                    G::V3 flat = (point - c) - up * height;
                    if (G::length(flat) > radius) flat = G::normalized(flat) * radius;
                    toward = (c + flat) - point;
                    distance = G::length(toward);
                    if (!inRange(distance)) return;
                } else if (name == "GlobalSegmentGravity") {
                    const G::V3 a = c, b = c + up * (1000.0f * tr.scale.y);
                    const G::V3 ab = b - a;
                    const float k = std::clamp(G::dot(point - a, ab) / std::max(G::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
                    toward = (a + ab * k) - point;
                    distance = G::length(toward);
                    if (!inRange(distance)) return;
                } else {
                    // Point (Distant = 500 x scale_x, its sphere), cones, tori, wires: toward the centre.
                    if (name == "GlobalPointGravity" && distantField < 0) distant = s.x;
                    toward = c - point;
                    distance = G::length(toward);
                    if (!inRange(distance)) return;
                }
                const int priority = intCell(model, row, "Priority", 0);
                if (priority > bestPriority || (priority == bestPriority && distance < bestDistance)) {
                    bestPriority = priority;
                    bestDistance = distance;
                    const bool inverse = intCell(model, row, "Inverse", -1) > 0;
                    best.down = G::normalized(toward, {0, -1, 0}) * (inverse ? -1.0f : 1.0f);
                    best.source = name + " " + rowRef(model, row);
                    best.row = row;
                }
            });
        }
    }
    if (!best.source.empty()) return best;
    if (std::optional< Hit > hit = nearest(point, 1e30f)) {
        best.down = G::normalized(hit->point - point, {0, -1, 0});
        best.source = "nearest surface (" + rowRef(model, hit->row) + ")";
        best.row = hit->row;
        return best;
    }
    best.source = "default (world -Y)";
    return best;
}

}  // namespace PetariEditor::LevelApi

namespace PetariEditor::LevelApi {

Level::Impl::Impl() = default;
Level::Impl::~Impl() {
    if (!ownedBaseDirectory.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(ownedBaseDirectory, ec);
    }
}

World& Level::Impl::world() {
    if (!worldCache) worldCache = std::make_unique< World >(mountedSource);
    worldCache->sync(*model);
    return *worldCache;
}

}  // namespace PetariEditor::LevelApi
