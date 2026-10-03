// Reading KCL back as triangles, and rays against them (kcl_reader.hpp).
#include <petari/editor/terrain/kcl_reader.hpp>
#include <cmath>
#include <cstring>

namespace PetariEditor::Terrain {

namespace {
    using Bytes = Formats::Bytes;

    std::uint32_t get32(const Bytes& b, std::size_t at) {
        return static_cast< std::uint32_t >(b[at]) << 24 | static_cast< std::uint32_t >(b[at + 1]) << 16 |
               static_cast< std::uint32_t >(b[at + 2]) << 8 | b[at + 3];
    }
    std::uint16_t get16(const Bytes& b, std::size_t at) { return static_cast< std::uint16_t >(b[at] << 8 | b[at + 1]); }
    float getF(const Bytes& b, std::size_t at) {
        const std::uint32_t bits = get32(b, at);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }

    Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    Vec3 mul(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

    bool fail(std::string* pError, const char* pText) {
        if (pError != nullptr) {
            *pError = pText;
        }
        return false;
    }
}  // namespace

std::optional< Mesh > readKcl(const Bytes& kcl, std::string* pError) {
    if (kcl.size() < 0x38) {
        fail(pError, "too small for a KCL header");
        return std::nullopt;
    }
    const std::uint32_t posOffset = get32(kcl, 0x00), nrmOffset = get32(kcl, 0x04), prismOffset = get32(kcl, 0x08),
                        octreeOffset = get32(kcl, 0x0C);
    // Prisms are indexed from 1 and end where the octree starts.
    if (posOffset > nrmOffset || nrmOffset > octreeOffset || octreeOffset > kcl.size() || prismOffset + 0x10 > octreeOffset) {
        fail(pError, "the KCL's sections are out of order");
        return std::nullopt;
    }
    const std::size_t prisms = (octreeOffset - prismOffset - 0x10) / 0x10;
    const std::size_t positions = (nrmOffset - posOffset) / 12;
    auto vec = [&](std::uint32_t base, std::size_t index) {
        const std::size_t at = base + index * 12;
        return Vec3{getF(kcl, at), getF(kcl, at + 4), getF(kcl, at + 8)};
    };

    Mesh mesh;
    mesh.positions.reserve(prisms * 3);
    mesh.indices.reserve(prisms * 3);
    mesh.triangleSurface.reserve(prisms);
    for (std::size_t i = 1; i <= prisms; i++) {
        const std::size_t at = prismOffset + i * 0x10;
        const float height = getF(kcl, at);
        const std::uint16_t pos = get16(kcl, at + 4), nrm = get16(kcl, at + 6);
        const std::uint16_t edge[3] = {get16(kcl, at + 8), get16(kcl, at + 10), get16(kcl, at + 12)};
        // Normals run up to the prisms, which overlap the last ones (prism 0).
        const std::size_t maxNormal = (octreeOffset - nrmOffset) / 12;
        if (pos >= positions || nrm >= maxNormal || edge[0] >= maxNormal || edge[1] >= maxNormal || edge[2] >= maxNormal) {
            fail(pError, "a prism indexes past its positions or normals");
            return std::nullopt;
        }
        // KCollisionServer::getPos: the edges v1-v3 (A) and v1-v2 (B) run along cross(edge
        // normal, face normal); the height is v1's distance to the edge v2-v3 (C).
        const Vec3 v1 = vec(posOffset, pos);
        const Vec3 face = vec(nrmOffset, nrm);
        const Vec3 a = vec(nrmOffset, edge[0]), b = vec(nrmOffset, edge[1]), c = vec(nrmOffset, edge[2]);
        const Vec3 alongA = cross(a, face), alongB = cross(b, face);
        const float dotB = dot(alongB, c), dotA = dot(alongA, c);
        // A degenerate prism (the game's queries never hit it) stays as a point, so triangle i is
        // prism i + 1.
        const Vec3 v2 = dotB != 0.0f ? add(v1, mul(alongB, height / dotB)) : v1;
        const Vec3 v3 = dotA != 0.0f ? add(v1, mul(alongA, height / dotA)) : v1;
        const std::uint32_t base = static_cast< std::uint32_t >(mesh.positions.size());
        mesh.positions.push_back(v1);
        mesh.positions.push_back(v2);
        mesh.positions.push_back(v3);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.triangleSurface.push_back(get16(kcl, at + 14));
    }
    return mesh;
}

std::optional< RayHit > raycast(const Mesh& mesh, const Vec3& origin, const Vec3& direction, float maxDistance) {
    const float length = std::sqrt(dot(direction, direction));
    if (length == 0.0f) {
        return std::nullopt;
    }
    const Vec3 d = mul(direction, 1.0f / length);
    std::optional< RayHit > best;
    for (std::size_t t = 0; t * 3 + 2 < mesh.indices.size(); t++) {
        // Moller-Trumbore, both sides.
        const Vec3& v0 = mesh.positions[mesh.indices[t * 3]];
        const Vec3 e1 = sub(mesh.positions[mesh.indices[t * 3 + 1]], v0);
        const Vec3 e2 = sub(mesh.positions[mesh.indices[t * 3 + 2]], v0);
        const Vec3 p = cross(d, e2);
        const float det = dot(e1, p);
        if (std::fabs(det) < 1e-12f) {
            continue;
        }
        const float inv = 1.0f / det;
        const Vec3 s = sub(origin, v0);
        const float u = dot(s, p) * inv;
        if (u < 0.0f || u > 1.0f) {
            continue;
        }
        const Vec3 q = cross(s, e1);
        const float v = dot(d, q) * inv;
        if (v < 0.0f || u + v > 1.0f) {
            continue;
        }
        const float distance = dot(e2, q) * inv;
        if (distance < 0.0f || distance > maxDistance || (best && distance >= best->distance)) {
            continue;
        }
        Vec3 n = cross(e1, e2);
        const float nl = std::sqrt(dot(n, n));
        n = nl > 0.0f ? mul(n, 1.0f / nl) : Vec3{0.0f, 1.0f, 0.0f};
        RayHit hit;
        hit.point = add(origin, mul(d, distance));
        hit.normal = n;
        hit.distance = distance;
        hit.triangle = t;
        hit.front = dot(n, d) < 0.0f;
        best = hit;
    }
    return best;
}

}  // namespace PetariEditor::Terrain
