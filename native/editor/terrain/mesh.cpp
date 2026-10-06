// Terrain meshes and brush primitives. See petari/editor/terrain/mesh.hpp.
#include <petari/editor/terrain/mesh.hpp>
#include <algorithm>
#include <cmath>

namespace PetariEditor::Terrain {

namespace {
    // The game's CollisionCode tables (src/Game/Map/CollisionCode.cpp), in enum order.
    const std::vector< std::string > kFloor = {
        "Normal",       "Death",        "Slip",         "NoSlip",     "DamageNormal", "Ice",       "JumpLow",
        "JumpMiddle",   "JumpHigh",     "Slider",       "DamageFire", "JumpNormal",   "FireDance", "Sand",
        "Glass",        "DamageElectric", "PullBack",   "Sink",       "SinkPoison",   "Slide",     "WaterBottomH",
        "WaterBottomM", "WaterBottomL", "Wet",          "Needle",     "SinkDeath",    "Snow",      "RailMove",
        "AreaMove",     "Press",        "NoStampSand",  "SinkDeathMud", "Brake",      "GlassIce",  "JumpParasol",
    };
    const std::vector< std::string > kWall = {"Normal", "NotWallJump", "NotWallSlip", "NotGrab", "GhostThroughCode",
                                              "NotSideStep", "Rebound", "Fur", "NoAction"};
    const std::vector< std::string > kSound = {"null", "Soil", "Lawn", "Stone", "Marble", "WoodThick", "WoodThin", "Metal",
                                               "Snow", "Ice", "Shallow", "Sand", "Beach", "Carpet", "Mud", "Honey"};

    int find(const std::vector< std::string >& table, const std::string& name) {
        auto it = std::find(table.begin(), table.end(), name);
        return it == table.end() ? -1 : static_cast< int >(it - table.begin());
    }
}  // namespace

int floorCode(const std::string& name) { return find(kFloor, name); }
int wallCode(const std::string& name) { return find(kWall, name); }
int soundCode(const std::string& name) { return find(kSound, name); }
const std::vector< std::string >& floorCodeNames() { return kFloor; }
const std::vector< std::string >& wallCodeNames() { return kWall; }
const std::vector< std::string >& soundCodeNames() { return kSound; }

void Mesh::append(const Mesh& other) {
    const std::uint32_t base = static_cast< std::uint32_t >(positions.size());
    const bool hadNormals = !normals.empty() || positions.empty();
    const bool hadUvs = !uvs.empty() || positions.empty();
    // Surfaces: map the other mesh's to ours (shared when equal).
    std::vector< std::uint16_t > remap;
    if (surfaces.empty()) {
        surfaces.push_back(Surface());
    }
    if (triangleSurface.empty()) {
        triangleSurface.assign(triangleCount(), 0);
    }
    const std::vector< Surface > otherSurfaces = other.surfaces.empty() ? std::vector< Surface >{Surface()} : other.surfaces;
    for (const Surface& s : otherSurfaces) {
        auto it = std::find(surfaces.begin(), surfaces.end(), s);
        if (it == surfaces.end()) {
            surfaces.push_back(s);
            it = surfaces.end() - 1;
        }
        remap.push_back(static_cast< std::uint16_t >(it - surfaces.begin()));
    }
    positions.insert(positions.end(), other.positions.begin(), other.positions.end());
    if (hadNormals && other.normals.size() == other.positions.size()) {
        normals.insert(normals.end(), other.normals.begin(), other.normals.end());
    } else {
        normals.clear();
    }
    if (hadUvs && other.uvs.size() == other.positions.size() * 2) {
        uvs.insert(uvs.end(), other.uvs.begin(), other.uvs.end());
    } else {
        uvs.clear();
    }
    for (std::uint32_t index : other.indices) {
        indices.push_back(base + index);
    }
    for (std::size_t t = 0; t < other.triangleCount(); t++) {
        triangleSurface.push_back(remap[other.triangleSurface.empty() ? 0 : other.triangleSurface[t]]);
    }
}

void Mesh::bounds(Vec3* pMin, Vec3* pMax) const {
    Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    for (std::size_t i = 0; i < positions.size(); i++) {
        const Vec3& p = positions[i];
        if (i == 0) {
            lo = hi = p;
            continue;
        }
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    *pMin = lo;
    *pMax = hi;
}

Mesh makeBox(const Vec3& c, const Vec3& h, const Surface& surface, float uvScale) {
    Mesh mesh;
    mesh.surfaces.push_back(surface);
    // Per face: the normal axis and sign, and the two in-plane axes (u, v) such that
    // u x v = normal, so the corners in (u, v) order wind counter-clockwise from outside.
    struct Face {
        int axis;
        float sign;
        int u, v;
    };
    const Face faces[6] = {{0, 1, 2, 1}, {0, -1, 1, 2}, {1, 1, 0, 2}, {1, -1, 2, 0}, {2, 1, 1, 0}, {2, -1, 0, 1}};
    const float center[3] = {c.x, c.y, c.z}, half[3] = {h.x, h.y, h.z};
    for (const Face& f : faces) {
        // u x v must equal sign * axis; flip v when the cyclic order does not give it.
        const bool cyclic = (f.u + 1) % 3 == f.v;
        const float vSign = (cyclic ? 1.0f : -1.0f) * f.sign;
        const std::uint32_t base = static_cast< std::uint32_t >(mesh.positions.size());
        const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const auto& corner : corners) {
            float p[3];
            p[f.axis] = center[f.axis] + f.sign * half[f.axis];
            p[f.u] = center[f.u] + corner[0] * half[f.u];
            p[f.v] = center[f.v] + corner[1] * vSign * half[f.v];
            mesh.positions.push_back({p[0], p[1], p[2]});
            float n[3] = {0, 0, 0};
            n[f.axis] = f.sign;
            mesh.normals.push_back({n[0], n[1], n[2]});
            // Texture lock: world-aligned planar projection on the face's plane.
            mesh.uvs.push_back(p[f.u] / uvScale);
            mesh.uvs.push_back(p[f.v] / uvScale * vSign);
        }
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
            mesh.indices.push_back(base + i);
        }
    }
    mesh.triangleSurface.assign(mesh.triangleCount(), 0);
    return mesh;
}

Mesh makeSphere(const Vec3& c, float radius, int rings, int segments, const Surface& surface, float uvScale) {
    Mesh mesh;
    mesh.surfaces.push_back(surface);
    rings = std::max(rings, 2);
    segments = std::max(segments, 3);
    const float pi = 3.14159265358979f;
    const float circumference = 2.0f * pi * radius;
    for (int r = 0; r <= rings; r++) {
        const float theta = pi * static_cast< float >(r) / static_cast< float >(rings);  // from +y down
        for (int s = 0; s <= segments; s++) {
            const float phi = 2.0f * pi * static_cast< float >(s) / static_cast< float >(segments);
            const Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi)};
            mesh.positions.push_back({c.x + radius * n.x, c.y + radius * n.y, c.z + radius * n.z});
            mesh.normals.push_back(n);
            mesh.uvs.push_back(circumference * static_cast< float >(s) / static_cast< float >(segments) / uvScale);
            mesh.uvs.push_back(circumference * 0.5f * static_cast< float >(r) / static_cast< float >(rings) / uvScale);
        }
    }
    const std::uint32_t stride = static_cast< std::uint32_t >(segments + 1);
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            const std::uint32_t a = r * stride + s, b = a + 1, d = a + stride, e = d + 1;
            // Rows run from the north pole down and segments eastward (around +y), so
            // (a, d, e) and (a, e, b) wind counter-clockwise seen from outside. The pole
            // rows' degenerate triangles are left out.
            if (r != 0) {
                mesh.indices.insert(mesh.indices.end(), {a, d, b});
            }
            if (r != rings - 1) {
                mesh.indices.insert(mesh.indices.end(), {b, d, e});
            }
        }
    }
    mesh.triangleSurface.assign(mesh.triangleCount(), 0);
    return mesh;
}

}  // namespace PetariEditor::Terrain
