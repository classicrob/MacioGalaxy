// The level editor's KCL writer (native/editor/terrain/kcl_writer.cpp) round-tripped through the
// game's own collision code: every generated .kcl is converted as ResourceHolder converts disc
// files (PetariNative::KCL::convertInPlace), registered with KCollisionServer::init, and queried.
//
// - Box, sphere and a bumpy heightfield brush: the server reports the triangle count, getPos
//   reconstructs every triangle's corners from the prism (height + normals), and arrows agree
//   with a brute-force ray/triangle test over all triangles (so the octree loses nothing).
// - Spheres resting on the box's top face are found by checkSphere.
// - The .pa: JMapInfo reads each surface's codes back through the game's masked fields, and a
//   prism's attribute row is its triangle's surface.
// - Limits: an empty mesh, degenerate triangles and more than 65535 prisms or normals.
// - With the disc (PETARI_TEST_DISC_FILES): planets' .kcl files are decoded through the game's
//   server (getPos, attributes), rebuilt by the writer, and the same arrows are cast at both:
//   they must hit the same prism (the writer keeps prism order) away from edges.
//
// Links: src/Game/Map/KCollision.cpp, KCollisionPlus.cpp, src/Game/Util/JMapInfo.cpp, MathUtil.cpp,
// native/resource (kcl_collision), petari_editor_terrain. The camera-code hooks KCollision.cpp
// references are aborting stubs: queries here never attach a .pa to a server.
#include "Game/Map/KCollision.hpp"
#include "Game/Util/JMapInfo.hpp"
#include <petari/editor/terrain/brush.hpp>
#include <petari/editor/terrain/kcl_reader.hpp>
#include <petari/editor/terrain/kcl_writer.hpp>
#include <petari/editor/formats/formats.hpp>
#include <petari/kcl_collision.hpp>
#include <chrono>
#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

class CollisionCode;
class CollisionDirector;

namespace MR {
    [[noreturn]] static void unreachable(const char* pName) {
        std::fprintf(stderr, "editor_terrain_kcl_tests: unexpected call to %s\n", pName);
        std::abort();
    }
    CollisionDirector* getCollisionDirector() { unreachable("MR::getCollisionDirector"); }
    void registerCameraCode(u32) { unreachable("MR::registerCameraCode"); }
}  // namespace MR

class CollisionCode {
public:
    u32 getCameraID(const JMapInfoIter&);
};
u32 CollisionCode::getCameraID(const JMapInfoIter&) { MR::unreachable("CollisionCode::getCameraID"); }

namespace T = PetariEditor::Terrain;

static int sFailures = 0;
static void check(bool condition, const char* text) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", text);
        ++sFailures;
    }
}

static TVec3f tv(const T::Vec3& v) { return TVec3f(v.x, v.y, v.z); }

// Möller-Trumbore, front faces only (the game's arrows ignore back faces): the ray parameter
// in [0, 1] of the segment from `o` along `d`, or -1.
static double segmentHit(const TVec3f& o, const TVec3f& d, const TVec3f& a, const TVec3f& b, const TVec3f& c) {
    const double e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
    const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    if (n[0] * d.x + n[1] * d.y + n[2] * d.z >= 0.0) {
        return -1.0;
    }
    const double p[3] = {d.y * e2[2] - d.z * e2[1], d.z * e2[0] - d.x * e2[2], d.x * e2[1] - d.y * e2[0]};
    const double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    const double s[3] = {o.x - a.x, o.y - a.y, o.z - a.z};
    const double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
    if (u < 0.0 || u > 1.0) {
        return -1.0;
    }
    const double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const double v = (d.x * q[0] + d.y * q[1] + d.z * q[2]) / det;
    if (v < 0.0 || u + v > 1.0) {
        return -1.0;
    }
    const double t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
    return t >= 0.0 && t <= 1.0 ? t : -1.0;
}

// Margin from a triangle's edges (in barycentric terms) inside which a brute-force hit is
// unambiguous: rays that graze an edge may legitimately land on either neighbour.
static bool nearEdge(const TVec3f& o, const TVec3f& d, const TVec3f& a, const TVec3f& b, const TVec3f& c) {
    const double e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
    const double p[3] = {d.y * e2[2] - d.z * e2[1], d.z * e2[0] - d.x * e2[2], d.x * e2[1] - d.y * e2[0]};
    const double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-9) {
        return true;
    }
    const double s[3] = {o.x - a.x, o.y - a.y, o.z - a.z};
    const double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
    const double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const double v = (d.x * q[0] + d.y * q[1] + d.z * q[2]) / det;
    const double m = 1e-3;
    return std::fabs(u) < m || std::fabs(v) < m || std::fabs(1.0 - u - v) < m;
}

struct Loaded {
    std::vector< u8 > data;
    KCollisionServer server;
    bool ok = false;
};

static bool load(const T::KclResult& r, Loaded* pOut, const char* pWhat) {
    if (!r.error.empty()) {
        std::fprintf(stderr, "FAIL: %s: buildKcl: %s\n", pWhat, r.error.c_str());
        ++sFailures;
        return false;
    }
    pOut->data = r.kcl;
    if (const char* pError = PetariNative::KCL::convertInPlace(pOut->data.data(), static_cast< u32 >(pOut->data.size()))) {
        std::fprintf(stderr, "FAIL: %s: convertInPlace: %s\n", pWhat, pError);
        ++sFailures;
        return false;
    }
    pOut->server.init(pOut->data.data(), nullptr);
    pOut->ok = true;
    return true;
}

// The prism for each non-degenerate triangle is prism i + 1 (the writer keeps their order).
static void checkMesh(const T::Mesh& mesh, const char* pWhat, int rays, unsigned seed) {
    const auto t0 = std::chrono::steady_clock::now();
    const T::KclResult r = T::buildKcl(mesh);
    const double ms = std::chrono::duration< double, std::milli >(std::chrono::steady_clock::now() - t0).count();
    Loaded loaded;
    if (!load(r, &loaded, pWhat)) {
        return;
    }
    KCollisionServer& server = loaded.server;
    std::printf("%s: %d triangles, %d positions, %d normals, %d top cells, %d branches, %d leaves, %d lists (max %d), %zu bytes, %.2f ms\n",
                pWhat, r.stats.triangles, r.stats.positions, r.stats.normals, r.stats.topCells, r.stats.branches, r.stats.leaves,
                r.stats.lists, r.stats.maxListLength, r.kcl.size(), ms);
    check(server.getTriangleNum() == static_cast< s32 >(mesh.triangleCount()) - r.stats.degenerate, "triangle count");
    check(r.stats.degenerate == 0, "no degenerate triangles in the brush");

    // getPos reconstructs the corners.
    double worst = 0.0;
    for (std::size_t t = 0; t < mesh.triangleCount(); t++) {
        KC_PrismData* pPrism = server.getPrismData(static_cast< u32 >(t));
        for (int k = 0; k < 3; k++) {
            const TVec3f got = server.getPos(pPrism, k);
            const T::Vec3& want = mesh.positions[mesh.indices[t * 3 + k]];
            worst = std::max(worst, static_cast< double >(std::fabs(got.x - want.x) + std::fabs(got.y - want.y) + std::fabs(got.z - want.z)));
        }
        check(!server.isNearParallelNormal(pPrism), "prism edge normals are not parallel");
    }
    if (worst > 0.05) {
        std::fprintf(stderr, "FAIL: %s: getPos is %.4f units off\n", pWhat, worst);
        ++sFailures;
    }

    // Arrows against brute force.
    T::Vec3 lo, hi;
    mesh.bounds(&lo, &hi);
    const TVec3f center((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2);
    const float span = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    std::mt19937 rng(seed);
    std::uniform_real_distribution< float > unit(-1.0f, 1.0f);
    int agree = 0, ambiguous = 0, hits = 0, wrong = 0;
    for (int i = 0; i < rays; i++) {
        TVec3f from(center.x + unit(rng) * span, center.y + unit(rng) * span, center.z + unit(rng) * span);
        TVec3f to(center.x + unit(rng) * span * 0.6f, center.y + unit(rng) * span * 0.6f, center.z + unit(rng) * span * 0.6f);
        const TVec3f dir(to.x - from.x, to.y - from.y, to.z - from.z);
        double best = 2.0;
        bool edge = false;
        for (std::size_t t = 0; t < mesh.triangleCount(); t++) {
            const TVec3f a = tv(mesh.positions[mesh.indices[t * 3]]), b = tv(mesh.positions[mesh.indices[t * 3 + 1]]),
                         c = tv(mesh.positions[mesh.indices[t * 3 + 2]]);
            const double h = segmentHit(from, dir, a, b, c);
            if (h >= 0.0) {
                best = std::min(best, h);
                edge = edge || nearEdge(from, dir, a, b, c);
            }
        }
        f32 dists[32];
        u8 flags[32];
        KC_PrismData* prisms[32];
        u32 count = 0;
        server.checkArrow(from, dir, dists, flags, &count, prisms, 32);
        const bool game = count != 0, brute = best <= 1.0;
        if (edge) {
            ambiguous++;
            continue;
        }
        hits += brute;
        if (game == brute) {
            agree++;
        } else {
            wrong++;
        }
    }
    std::printf("%s: %d rays, %d hit, %d agree, %d disagree, %d near an edge (skipped)\n", pWhat, rays, hits, agree, wrong, ambiguous);
    if (wrong != 0 || hits == 0) {
        std::fprintf(stderr, "FAIL: %s: the game's arrows disagree with brute force (%d) or never hit\n", pWhat, wrong);
        ++sFailures;
    }
}

static void testBox() {
    const T::Mesh box = T::makeBox({0, 0, 0}, {600, 100, 400});
    // Every face normal points out.
    for (std::size_t t = 0; t < box.triangleCount(); t++) {
        const T::Vec3 a = box.positions[box.indices[t * 3]], b = box.positions[box.indices[t * 3 + 1]], c = box.positions[box.indices[t * 3 + 2]];
        const float n[3] = {(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y), (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                            (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
        const float centroid[3] = {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
        check(n[0] * centroid[0] + n[1] * centroid[1] + n[2] * centroid[2] > 0.0f, "box faces wind outward");
    }
    checkMesh(box, "box", 4000, 1);

    // Mario-sized spheres resting on the top face touch it; one far above does not.
    Loaded loaded;
    if (!load(T::buildKcl(box), &loaded, "box sphere")) {
        return;
    }
    KC_PrismData* prisms[32];
    f32 dists[32];
    u8 flags[32];
    Fxyz on;
    on.x = 120.0f;
    on.y = 100.0f + 40.0f;
    on.z = -50.0f;
    const u32 found = loaded.server.checkSphere(&on, 50.0f, 1.0f, 32, prisms, dists, flags);
    bool up = false;
    for (u32 i = 0; i < found; i++) {
        up = up || loaded.server.getFaceNormal(prisms[i])->y > 0.99f;
    }
    check(found > 0 && up, "a sphere resting on the top face hits it");
    Fxyz above = on;
    above.y = 400.0f;
    check(loaded.server.checkSphere(&above, 50.0f, 1.0f, 32, prisms, dists, flags) == 0, "a sphere far above hits nothing");
}

static void testSphere() {
    const T::Mesh sphere = T::makeSphere({100, -50, 25}, 700.0f, 16, 24);
    for (std::size_t t = 0; t < sphere.triangleCount(); t++) {
        const T::Vec3 a = sphere.positions[sphere.indices[t * 3]], b = sphere.positions[sphere.indices[t * 3 + 1]],
                      c = sphere.positions[sphere.indices[t * 3 + 2]];
        const float n[3] = {(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y), (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                            (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
        const float centroid[3] = {(a.x + b.x + c.x) / 3 - 100, (a.y + b.y + c.y) / 3 + 50, (a.z + b.z + c.z) / 3 - 25};
        if (n[0] * centroid[0] + n[1] * centroid[1] + n[2] * centroid[2] <= 0.0f) {
            check(false, "sphere faces wind outward");
            break;
        }
    }
    checkMesh(sphere, "sphere", 3000, 2);
}

// A bumpy heightfield of 2 n^2 triangles: a deep octree with shared lists. Every triangle has
// its own face and edge normals here, so 4 per triangle: n = 90 (16,200 triangles, 64,800
// normals) fits the u16 normal index, n = 91 does not.
static T::Mesh heightfield(int n) {
    T::Mesh mesh;
    const float step = 80.0f;
    for (int z = 0; z <= n; z++) {
        for (int x = 0; x <= n; x++) {
            const float h = 300.0f * std::sin(x * 0.21f) * std::cos(z * 0.17f) + 40.0f * std::sin(x * 1.3f + z * 0.7f);
            mesh.positions.push_back({x * step - 3000.0f, h, z * step - 4000.0f});
        }
    }
    for (int z = 0; z < n; z++) {
        for (int x = 0; x < n; x++) {
            const std::uint32_t a = z * (n + 1) + x, b = a + 1, c = a + (n + 1), d = c + 1;
            // Up-facing: (a, c, b) and (b, c, d) wind counter-clockwise seen from +y.
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}

// Every brush shape is closed and winds outward (each face normal points away from the brush's
// centre of mass), and its collision answers arrows like brute force.
static void testBrushes() {
    for (int shape = 0; shape < 4; shape++) {
        const T::Brush brush = T::brushFromRow(shape, 3, 1, 1, {1.6f, 0.8f, 2.4f});
        const T::Mesh mesh = T::brushMesh(brush);
        double cx = 0, cy = 0, cz = 0;
        for (const T::Vec3& p : mesh.positions) {
            cx += p.x;
            cy += p.y;
            cz += p.z;
        }
        cx /= mesh.positions.size();
        cy /= mesh.positions.size();
        cz /= mesh.positions.size();
        int inward = 0;
        T::Vec3 lo, hi;
        mesh.bounds(&lo, &hi);
        for (std::size_t t = 0; t < mesh.triangleCount(); t++) {
            const T::Vec3 a = mesh.positions[mesh.indices[t * 3]], b = mesh.positions[mesh.indices[t * 3 + 1]],
                          c = mesh.positions[mesh.indices[t * 3 + 2]];
            const double n[3] = {(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y), (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                                 (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
            const double m[3] = {(a.x + b.x + c.x) / 3 - cx, (a.y + b.y + c.y) / 3 - cy, (a.z + b.z + c.z) / 3 - cz};
            inward += n[0] * m[0] + n[1] * m[1] + n[2] * m[2] <= 0.0;
        }
        const std::string name = "brush " + T::brushShapeNames()[shape];
        check(inward == 0, (name + ": faces wind outward").c_str());
        check(std::fabs(lo.y) < 0.01f && mesh.normals.size() == mesh.positions.size() && mesh.uvs.size() == mesh.positions.size() * 2,
              (name + ": bottom at the origin, with normals and UVs").c_str());
        check(mesh.surfaces[0].floor == T::floorCode("Ice") && mesh.surfaces[0].sound == T::soundCode("Stone"),
              (name + ": surface from the presets (Ice floor, Stone look)").c_str());
        checkMesh(mesh, name.c_str(), 1500, 10 + shape);
        const T::GravityFit fit = T::fitGravity(brush);
        check(fit.kind != T::GravityFit::Kind::None && fit.range >= 1000.0f, (name + ": fitted gravity").c_str());
    }
    const T::Brush sphere = T::brushFromRow(1, -1, -1, -1, {2, 5, 9});
    check(sphere.size.y == 2000 && sphere.size.z == 2000 && sphere.gravity == T::GravityMode::Fitted && sphere.material == 0, "a sphere is round; -1 args default");
    check(T::fitGravity(T::brushFromRow(0, 0, 0, 0, {1, 1, 1})).kind == T::GravityFit::Kind::None, "gravity 0: the stage's own");
    check(T::brushFromRow(9, 99, 99, 1, {0, 0, 0}).size.x >= 50.0f, "args and sizes clamp");
    // Gravity 2, down only: a cube over the bounds with only its top face, for any shape.
    const T::GravityFit down = T::fitGravity(T::brushFromRow(1, 0, 0, 2, {2, 2, 2}));
    check(T::brushFromRow(0, 0, 0, 2, {1, 1, 1}).gravity == T::GravityMode::DownOnly && T::gravityModeNames().size() == 3, "gravity 2: down only");
    check(down.kind == T::GravityFit::Kind::Cube && down.activeFaces == 4 && std::fabs(down.half.x - 1000.0f) < 0.01f &&
              std::fabs(down.center.y - 1000.0f) < 0.01f,
          "down only: the sphere's bounds, top face only");
}

// A playtester's thin wooden cylinder (650 wide, 150 tall, down-only gravity; EP report): every
// vertical arrow over its cap hits the cap in the game's own collision server.
static void testThinCylinder() {
    const T::Brush brush = T::brushFromRow(2, 4, 0, 2, {0.65f, 0.15f, 0.65f});
    const T::Mesh mesh = T::brushMesh(brush);
    checkMesh(mesh, "thin cylinder", 4000, 31);
    Loaded loaded;
    if (!load(T::buildKcl(mesh), &loaded, "thin cylinder")) {
        return;
    }
    int probes = 0, misses = 0;
    float worstX = 0, worstZ = 0;
    for (float x = -318.0f; x <= 318.0f; x += 7.0f) {
        for (float z = -318.0f; z <= 318.0f; z += 7.0f) {
            if (x * x + z * z > 318.0f * 318.0f) {
                continue;
            }
            probes++;
            f32 dists[8];
            u8 flags[8];
            KC_PrismData* prisms[8];
            u32 count = 0;
            loaded.server.checkArrow(TVec3f(x, 300.0f, z), TVec3f(0.0f, -200.0f, 0.0f), dists, flags, &count, prisms, 8);
            if (count == 0) {
                misses++;
                worstX = x;
                worstZ = z;
            }
        }
    }
    std::printf("thin cylinder cap: %d vertical arrows, %d miss%s\n", probes, misses,
                misses != 0 ? (" (e.g. at " + std::to_string(worstX) + ", " + std::to_string(worstZ) + ")").c_str() : "");
    check(misses == 0, "every vertical arrow over the thin cylinder's cap hits it");
}

// The reader (kcl_reader.hpp) on the writer's output: the same triangles; rays meet them.
static void testReader() {
    const T::Mesh box = T::makeBox({0, 150, 0}, {1500, 150, 1500});
    const T::KclResult kcl = T::buildKcl(box);
    std::string error;
    const auto read = T::readKcl(kcl.kcl, &error);
    check(read.has_value() && read->triangleCount() == box.triangleCount(), "read back: one triangle per prism");
    if (!read) {
        return;
    }
    float worst = 0.0f;
    for (std::size_t t = 0; t < box.triangleCount(); t++) {
        for (int k = 0; k < 3; k++) {
            const T::Vec3 a = box.positions[box.indices[t * 3 + k]], b = read->positions[read->indices[t * 3 + k]];
            worst = std::max({worst, std::fabs(a.x - b.x), std::fabs(a.y - b.y), std::fabs(a.z - b.z)});
        }
    }
    check(worst < 0.05f, "read back: the written vertices, in order");
    const auto hit = T::raycast(*read, {100, 1000, -200}, {0, -1, 0}, 5000.0f);
    check(hit && std::fabs(hit->point.y - 300.0f) < 0.01f && hit->normal.y > 0.99f && hit->front && std::fabs(hit->distance - 700.0f) < 0.01f,
          "a ray down meets the box's top at y = 300");
    const auto under = T::raycast(*read, {100, 100, -200}, {0, 1, 0}, 5000.0f);
    check(under && std::fabs(under->point.y - 300.0f) < 0.01f && !under->front, "from inside: the top, from behind");
    check(!T::raycast(*read, {100, 1000, -200}, {0, 1, 0}, 5000.0f) && !T::raycast(*read, {100, 1000, -200}, {0, -1, 0}, 500.0f),
          "rays away or too short miss");
    check(!T::readKcl(PetariEditor::Formats::Bytes(16, 0)).has_value(), "a short file is refused");
}

static void testHeightfield() {
    checkMesh(heightfield(90), "heightfield", 1500, 3);
    const T::KclResult r = T::buildKcl(heightfield(91));
    check(!r.error.empty() && r.stats.normals == 0, "a mesh needing more than 65535 normals is refused");
}

static void testAttributes() {
    // Two surfaces: the box's top face is Ice/Snow sound/NotWallJump, the rest Normal.
    T::Mesh box = T::makeBox({0, 0, 0}, {300, 300, 300});
    T::Surface ice;
    ice.floor = T::floorCode("Ice");
    ice.wall = T::wallCode("NotWallJump");
    ice.sound = T::soundCode("Snow");
    ice.cameraThrough = 1;
    box.surfaces.push_back(ice);
    int iceTriangles = 0;
    for (std::size_t t = 0; t < box.triangleCount(); t++) {
        float ny = box.normals[box.indices[t * 3]].y;
        if (ny > 0.5f) {
            box.triangleSurface[t] = 1;
            iceTriangles++;
        }
    }
    check(iceTriangles == 2 && ice.floor == 5 && ice.wall == 1 && ice.sound == 8, "code names match the game's enums");
    const T::KclResult r = T::buildKcl(box);
    check(r.error.empty(), "attributed box builds");

    JMapInfo info;
    std::vector< u8 > pa = r.pa;
    check(pa.size() % 32 == 0, ".pa padded to 32 bytes");
    check(info.attach(pa.data()), ".pa attaches as a JMapInfo");
    check(info.getNumEntries() == 2, ".pa has one row per surface");
    u32 floor = 99, wall = 99, sound = 99, camera = 99, cameraId = 0;
    JMapInfoIter row(&info, 1);
    row.getValue< u32 >("Floor_code", &floor);
    row.getValue< u32 >("Wall_code", &wall);
    row.getValue< u32 >("Sound_code", &sound);
    row.getValue< u32 >("Camera_through", &camera);
    row.getValue< u32 >("camera_id", &cameraId);
    check(floor == 5 && wall == 1 && sound == 8 && camera == 1 && cameraId == 0xFF, "row 1 reads back Ice/NotWallJump/Snow/Through");
    JMapInfoIter normal(&info, 0);
    normal.getValue< u32 >("Floor_code", &floor);
    normal.getValue< u32 >("Wall_code", &wall);
    check(floor == 0 && wall == 0, "row 0 reads back Normal");
    check(info.getValueType("Floor_code") == 0, "codes are packed (masked) longs, as on the disc");

    // Every code the game defines fits its field.
    T::Surface max;
    max.floor = static_cast< int >(T::floorCodeNames().size()) - 1;
    max.wall = static_cast< int >(T::wallCodeNames().size()) - 1;
    max.sound = static_cast< int >(T::soundCodeNames().size()) - 1;
    std::vector< u8 > maxPa = T::buildPa({max});
    JMapInfo maxInfo;
    maxInfo.attach(maxPa.data());
    JMapInfoIter maxRow(&maxInfo, 0);
    maxRow.getValue< u32 >("Floor_code", &floor);
    maxRow.getValue< u32 >("Wall_code", &wall);
    maxRow.getValue< u32 >("Sound_code", &sound);
    check(floor == 34 && wall == 8 && sound == 15, "the largest codes fit (JumpParasol, NoAction, Honey)");

    // Prism attributes index the rows.
    Loaded loaded;
    if (load(r, &loaded, "attributes")) {
        int iceFound = 0;
        for (s32 i = 0; i < loaded.server.getTriangleNum(); i++) {
            KC_PrismData* pPrism = loaded.server.getPrismData(i);
            const bool top = loaded.server.getFaceNormal(pPrism)->y > 0.5f;
            check((pPrism->mAttribute == 1) == top, "a prism's attribute is its triangle's surface");
            iceFound += pPrism->mAttribute == 1;
        }
        check(iceFound == 2, "two ice prisms");
    }
}

static void testLimits() {
    T::Mesh empty;
    check(!T::buildKcl(empty).error.empty() && T::buildKcl(empty).kcl.empty(), "an empty mesh is refused");

    T::Mesh mixed = T::makeBox({0, 0, 0}, {100, 100, 100});
    mixed.positions.push_back({5, 5, 5});
    mixed.indices.insert(mixed.indices.end(), {0, 0, 1});  // degenerate
    mixed.triangleSurface.push_back(0);
    const T::KclResult r = T::buildKcl(mixed);
    check(r.error.empty() && r.stats.degenerate == 1 && r.stats.triangles == 12, "degenerate triangles are skipped");

    T::Mesh bad = T::makeBox({0, 0, 0}, {100, 100, 100});
    bad.indices.push_back(9999);
    bad.indices.push_back(0);
    bad.indices.push_back(1);
    check(!T::buildKcl(bad).error.empty(), "an index past the positions is refused");

    // 66,000 separate tiny triangles: more prisms than u16 indices hold.
    T::Mesh huge;
    for (int i = 0; i < 66000; i++) {
        const float x = static_cast< float >(i % 300) * 10.0f, z = static_cast< float >(i / 300) * 10.0f;
        const std::uint32_t base = static_cast< std::uint32_t >(huge.positions.size());
        huge.positions.push_back({x, 0, z});
        huge.positions.push_back({x, 0, z + 5});
        huge.positions.push_back({x + 5, 0, z});
        huge.indices.insert(huge.indices.end(), {base, base + 1, base + 2});
    }
    const T::KclResult h = T::buildKcl(huge);
    check(!h.error.empty() && h.kcl.empty(), "more than 65535 prisms is refused (split the mesh)");
}

// Decodes a disc .kcl through the game's server into a mesh, rebuilds it with the writer and
// compares arrows. Prisms the game marks degenerate (non-positive height) are kept as
// triangles so indices line up; the writer then skips the ones that really are degenerate.
static void testDiscPlanet(const char* pDisc, const char* pName) {
    const std::string path = std::string(pDisc) + "/ObjectData/" + pName + ".arc";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::printf("disc %s: not found, skipped\n", pName);
        return;
    }
    const PetariEditor::Formats::Bytes raw((std::istreambuf_iterator< char >(file)), std::istreambuf_iterator< char >());
    auto archive = PetariEditor::Formats::Archive::parse(raw);
    const PetariEditor::Formats::Bytes* pKcl = archive ? archive->file(std::string(pName) + ".kcl") : nullptr;
    if (pKcl == nullptr) {
        check(false, "disc planet archive has its .kcl");
        return;
    }
    std::vector< u8 > original = *pKcl;
    check(PetariNative::KCL::convertInPlace(original.data(), static_cast< u32 >(original.size())) == nullptr, "disc .kcl converts");
    KCollisionServer disc;
    disc.init(original.data(), nullptr);

    // The reader rebuilds the disc's triangles as the game's getPos does.
    const auto read = T::readKcl(*pKcl);
    check(read.has_value() && static_cast< s32 >(read->triangleCount()) <= disc.getTriangleNum() && read->triangleCount() > 0,
          "the reader reads a disc .kcl");
    if (read && static_cast< s32 >(read->triangleCount()) == disc.getTriangleNum()) {
        float worst = 0.0f;
        for (s32 i = 0; i < disc.getTriangleNum(); i++) {
            KC_PrismData* pPrism = disc.getPrismData(i);
            if (pPrism->mHeight <= 0.0f || disc.isNearParallelNormal(pPrism)) {
                continue;
            }
            for (int k = 0; k < 3; k++) {
                const T::Vec3 r = read->positions[read->indices[i * 3 + k]];
                float nearest = 1e30f;
                for (int g = 0; g < 3; g++) {
                    const TVec3f p = disc.getPos(pPrism, g);
                    nearest = std::min(nearest, std::max({std::fabs(p.x - r.x), std::fabs(p.y - r.y), std::fabs(p.z - r.z)}));
                }
                worst = std::max(worst, nearest);
            }
        }
        check(worst < 0.5f, "the reader's triangles are the game's (getPos)");
        std::printf("disc %s: reader matches getPos within %.4f\n", pName, worst);
    } else {
        check(false, "the reader keeps every disc prism");
    }

    T::Mesh mesh;
    std::vector< int > prismOfTriangle;
    for (s32 i = 0; i < disc.getTriangleNum(); i++) {
        KC_PrismData* pPrism = disc.getPrismData(i);
        if (pPrism->mHeight <= 0.0f || disc.isNearParallelNormal(pPrism)) {
            continue;  // never hit by the game's queries
        }
        for (int k = 0; k < 3; k++) {
            const TVec3f p = disc.getPos(pPrism, k);
            mesh.indices.push_back(static_cast< std::uint32_t >(mesh.positions.size()));
            mesh.positions.push_back({p.x, p.y, p.z});
        }
        prismOfTriangle.push_back(i);
    }
    const T::KclResult r = T::buildKcl(mesh);
    Loaded rebuilt;
    if (!load(r, &rebuilt, pName)) {
        return;
    }
    check(r.stats.degenerate == 0, "disc triangles rebuild without degenerates");
    // Map rebuilt prism -> disc prism.
    T::Vec3 lo, hi;
    mesh.bounds(&lo, &hi);
    const TVec3f center((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2);
    const float span = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    std::mt19937 rng(7);
    std::uniform_real_distribution< float > unit(-1.0f, 1.0f);
    int same = 0, different = 0, hits = 0;
    for (int i = 0; i < 3000; i++) {
        const TVec3f from(center.x + unit(rng) * span, center.y + unit(rng) * span, center.z + unit(rng) * span);
        const TVec3f to(center.x + unit(rng) * span * 0.5f, center.y + unit(rng) * span * 0.5f, center.z + unit(rng) * span * 0.5f);
        const TVec3f dir(to.x - from.x, to.y - from.y, to.z - from.z);
        f32 dists[64];
        u8 flags[64];
        KC_PrismData* prisms[64];
        u32 count = 0;
        // The nearest hit of each (the game sorts hits later; take the smallest distance here).
        auto nearest = [&](const KCollisionServer& rServer, int* pIndex) {
            count = 0;
            rServer.checkArrow(from, dir, dists, flags, &count, prisms, 64);
            *pIndex = -1;
            f32 best = 1e30f;
            for (u32 h = 0; h < count; h++) {
                if (dists[h] < best) {
                    best = dists[h];
                    *pIndex = rServer.toIndex(prisms[h]);
                }
            }
            return best;
        };
        int discIndex, newIndex;
        const f32 discDist = nearest(disc, &discIndex);
        const f32 newDist = nearest(rebuilt.server, &newIndex);
        if (newIndex >= 0) {
            newIndex = prismOfTriangle[newIndex];
        }
        hits += discIndex >= 0;
        if (discIndex == newIndex || (discIndex >= 0 && newIndex >= 0 && std::fabs(discDist - newDist) < 0.5f)) {
            same++;  // the same prism, or a tie at a shared edge (equal distance)
        } else {
            different++;
            if (different <= 3) {
                std::fprintf(stderr, "  %s arrow %d: disc prism %d (%.2f), rebuilt %d (%.2f)\n", pName, i, discIndex, discDist, newIndex, newDist);
            }
        }
    }
    std::printf("disc %s: %d triangles (%zu bytes on disc, %zu rebuilt); 3000 arrows, %d hit, %d same, %d different\n", pName,
                r.stats.triangles, pKcl->size(), r.kcl.size(), hits, same, different);
    if (different * 200 > hits || hits == 0) {
        std::fprintf(stderr, "FAIL: disc %s: the rebuilt collision answers arrows differently\n", pName);
        ++sFailures;
    }
}

int main() {
    testBox();
    testSphere();
    testHeightfield();
    testBrushes();
    testAttributes();
    testLimits();
    testReader();
    testThinCylinder();
#ifdef PETARI_TEST_DISC_FILES
    for (const char* pName : {"AssemblyBlockPlanet", "BroadBeanPlanet", "BathtubPlanet"}) {
        testDiscPlanet(PETARI_TEST_DISC_FILES, pName);
    }
#endif
    if (sFailures != 0) {
        std::fprintf(stderr, "%d terrain KCL check(s) failed\n", sFailures);
        return 1;
    }
    std::puts("terrain KCL writer tests passed");
    return 0;
}
