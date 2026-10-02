// KCL collision writer. See petari/editor/terrain/kcl_writer.hpp.
#include <petari/editor/terrain/kcl_writer.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

namespace PetariEditor::Terrain {

using Formats::Bytes;

namespace {
    struct V {
        double x, y, z;
    };
    V operator-(const V& a, const V& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    V cross(const V& a, const V& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    double dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    double length(const V& a) { return std::sqrt(dot(a, a)); }
    V normalized(const V& a) {
        const double l = length(a);
        return l > 0.0 ? V{a.x / l, a.y / l, a.z / l} : V{0, 0, 0};
    }
    V toV(const Vec3& v) { return {v.x, v.y, v.z}; }

    void put32(Bytes& b, std::size_t at, std::uint32_t v) {
        b[at] = static_cast< std::uint8_t >(v >> 24);
        b[at + 1] = static_cast< std::uint8_t >(v >> 16);
        b[at + 2] = static_cast< std::uint8_t >(v >> 8);
        b[at + 3] = static_cast< std::uint8_t >(v);
    }
    void put16(Bytes& b, std::size_t at, std::uint16_t v) {
        b[at] = static_cast< std::uint8_t >(v >> 8);
        b[at + 1] = static_cast< std::uint8_t >(v);
    }
    void putF(Bytes& b, std::size_t at, float f) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        put32(b, at, bits);
    }

    // A triangle in the octree's local space (relative to the header's min).
    struct Tri {
        V p[3];
    };

    // Triangle / axis-aligned box overlap (separating axis test, Akenine-Moller).
    bool triBoxOverlap(const Tri& t, const V& boxCenter, double half) {
        const V v0 = t.p[0] - boxCenter, v1 = t.p[1] - boxCenter, v2 = t.p[2] - boxCenter;
        const V e[3] = {v1 - v0, v2 - v1, v0 - v2};
        const V verts[3] = {v0, v1, v2};
        // The 9 edge x axis tests.
        for (const V& edge : e) {
            const V axes[3] = {{0, -edge.z, edge.y}, {edge.z, 0, -edge.x}, {-edge.y, edge.x, 0}};
            for (const V& a : axes) {
                double lo = 1e300, hi = -1e300;
                for (const V& v : verts) {
                    const double p = dot(a, v);
                    lo = std::min(lo, p);
                    hi = std::max(hi, p);
                }
                const double r = half * (std::fabs(a.x) + std::fabs(a.y) + std::fabs(a.z));
                if (lo > r || hi < -r) {
                    return false;
                }
            }
        }
        // The box's own axes.
        for (int axis = 0; axis < 3; axis++) {
            double lo = 1e300, hi = -1e300;
            for (const V& v : verts) {
                const double p = axis == 0 ? v.x : axis == 1 ? v.y : v.z;
                lo = std::min(lo, p);
                hi = std::max(hi, p);
            }
            if (lo > half || hi < -half) {
                return false;
            }
        }
        // The triangle's plane.
        const V n = cross(e[0], e[1]);
        const double d = dot(n, v0);
        const double r = half * (std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z));
        return std::fabs(d) <= r;
    }

    int log2Exact(std::uint64_t v) {
        int n = 0;
        while ((std::uint64_t(1) << n) < v) {
            n++;
        }
        return n;
    }

    // The octree before layout: node 0.. are the top-level cells in searchBlock order.
    struct Node {
        bool leaf = true;
        int list = -1;        // leaf: index into lists
        int children = -1;    // branch: index of the first of 8 consecutive nodes
    };

    class OctreeBuilder {
    public:
        OctreeBuilder(const std::vector< Tri >& tris, const KclOptions& options) : mTris(tris), mOptions(options) {}

        // Builds a cell of size 2^shift whose corner is `corner`, from the candidate prisms.
        int build(const V& corner, int shift, const std::vector< std::uint16_t >& candidates, std::vector< Node >* pNodes, int at) {
            const double size = std::ldexp(1.0, shift);
            const double half = size * 0.5;
            // A little slack so triangles exactly on a cell face land in both cells.
            const V center{corner.x + half, corner.y + half, corner.z + half};
            std::vector< std::uint16_t > inside;
            for (std::uint16_t prism : candidates) {
                if (triBoxOverlap(mTris[prism - 1], center, half + 0.5)) {
                    inside.push_back(prism);
                }
            }
            if (static_cast< int >(inside.size()) <= mOptions.maxPrismsPerLeaf || shift <= mOptions.minCellShift) {
                (*pNodes)[at].leaf = true;
                (*pNodes)[at].list = listIndex(inside);
                return at;
            }
            const int first = static_cast< int >(pNodes->size());
            pNodes->resize(pNodes->size() + 8);
            (*pNodes)[at].leaf = false;
            (*pNodes)[at].children = first;
            for (int i = 0; i < 8; i++) {
                const V c{corner.x + ((i & 1) ? half : 0.0), corner.y + ((i & 2) ? half : 0.0), corner.z + ((i & 4) ? half : 0.0)};
                build(c, shift - 1, inside, pNodes, first + i);
            }
            return at;
        }

        std::vector< std::vector< std::uint16_t > > mLists;

    private:
        int listIndex(const std::vector< std::uint16_t >& list) {
            auto it = mListIds.find(list);
            if (it != mListIds.end()) {
                return it->second;
            }
            const int id = static_cast< int >(mLists.size());
            mLists.push_back(list);
            mListIds.emplace(list, id);
            return id;
        }

        const std::vector< Tri >& mTris;
        const KclOptions& mOptions;
        std::map< std::vector< std::uint16_t >, int > mListIds;
    };

    // Shares normals that are equal within the tolerance (a grid of cells of that size; a
    // normal is compared with its own cell only, so near-equal normals across a cell boundary
    // stay distinct, which is merely less compact).
    class NormalPool {
    public:
        explicit NormalPool(float tolerance) : mScale(tolerance > 0.0f ? 1.0 / tolerance : 1e9) {}

        std::uint32_t add(const V& n) {
            const Key key{static_cast< std::int64_t >(std::llround(n.x * mScale)), static_cast< std::int64_t >(std::llround(n.y * mScale)),
                          static_cast< std::int64_t >(std::llround(n.z * mScale))};
            auto it = mIds.find(key);
            if (it != mIds.end()) {
                return it->second;
            }
            const std::uint32_t id = static_cast< std::uint32_t >(mNormals.size());
            mNormals.push_back(n);
            mIds.emplace(key, id);
            return id;
        }

        std::vector< V > mNormals;

    private:
        struct Key {
            std::int64_t x, y, z;
            bool operator<(const Key& o) const { return x != o.x ? x < o.x : y != o.y ? y < o.y : z < o.z; }
        };
        double mScale;
        std::map< Key, std::uint32_t > mIds;
    };
}  // namespace

Bytes buildPa(const std::vector< Surface >& surfaces) {
    struct Field {
        const char* name;
        std::uint32_t mask;
        std::uint8_t shift;
    };
    // The disc's field order; widths fit every code the game defines.
    const Field fields[5] = {{"camera_id", 0xFFu, 0},
                             {"Sound_code", 0x7Fu << 8, 8},
                             {"Floor_code", 0x3Fu << 15, 15},
                             {"Wall_code", 0xFu << 21, 21},
                             {"Camera_through", 0x1u << 25, 25}};
    const std::uint32_t rows = static_cast< std::uint32_t >(std::max< std::size_t >(surfaces.size(), 1));
    const std::uint32_t dataOffset = 16 + 5 * 12;
    const std::uint32_t size = dataOffset + rows * 4;
    Bytes out((size + 31) & ~31u, '@');
    put32(out, 0, rows);
    put32(out, 4, 5);
    put32(out, 8, dataOffset);
    put32(out, 12, 4);
    for (int i = 0; i < 5; i++) {
        const std::size_t at = 16 + i * 12;
        put32(out, at, Formats::jmapHash(fields[i].name));
        put32(out, at + 4, fields[i].mask);
        put16(out, at + 8, 0);
        out[at + 10] = fields[i].shift;
        out[at + 11] = 0;  // long (masked and shifted)
    }
    for (std::uint32_t r = 0; r < rows; r++) {
        const Surface s = r < surfaces.size() ? surfaces[r] : Surface();
        const std::uint32_t values[5] = {static_cast< std::uint32_t >(s.cameraId), static_cast< std::uint32_t >(s.sound),
                                         static_cast< std::uint32_t >(s.floor), static_cast< std::uint32_t >(s.wall),
                                         static_cast< std::uint32_t >(s.cameraThrough)};
        std::uint32_t word = 0;
        for (int i = 0; i < 5; i++) {
            word |= (values[i] << fields[i].shift) & fields[i].mask;
        }
        put32(out, dataOffset + r * 4, word);
    }
    return out;
}

KclResult buildKcl(const Mesh& mesh, const KclOptions& options) {
    KclResult result;
    KclStats& stats = result.stats;
    const std::size_t triCount = mesh.triangleCount();
    if (mesh.indices.size() % 3 != 0 || triCount == 0) {
        result.error = "the mesh has no triangles";
        return result;
    }
    for (std::uint32_t index : mesh.indices) {
        if (index >= mesh.positions.size()) {
            result.error = "a triangle index is past the positions";
            return result;
        }
    }
    const std::vector< Surface > surfaces = mesh.surfaces.empty() ? std::vector< Surface >{Surface()} : mesh.surfaces;

    // Bounds and the octree's space.
    Vec3 lo, hi;
    mesh.bounds(&lo, &hi);
    const V origin{lo.x - options.padding, lo.y - options.padding, lo.z - options.padding};
    const V extent{hi.x - lo.x + 2.0 * options.padding, hi.y - lo.y + 2.0 * options.padding, hi.z - lo.z + 2.0 * options.padding};
    const double longest = std::max({extent.x, extent.y, extent.z});
    int cellShift = std::max(options.minCellShift, log2Exact(static_cast< std::uint64_t >(std::ceil(longest / std::max(1, options.topCellsPerAxis)))));
    int axisShift[3];
    const double extents[3] = {extent.x, extent.y, extent.z};
    for (int a = 0; a < 3; a++) {
        const std::uint64_t cells = static_cast< std::uint64_t >(std::ceil(extents[a] / std::ldexp(1.0, cellShift)));
        axisShift[a] = log2Exact(std::max< std::uint64_t >(cells, 1));
    }
    if (cellShift + std::max({axisShift[0], axisShift[1], axisShift[2]}) > 30) {
        result.error = "the mesh is too large for the octree's 32-bit space";
        return result;
    }

    // Prisms: positions (shared when identical), normals (shared within the tolerance).
    std::vector< V > positions;
    std::map< std::tuple< float, float, float >, std::uint32_t > positionIds;
    NormalPool normals(options.normalTolerance);
    struct Prism {
        float height;
        std::uint32_t pos, nrm, edge[3];
        std::uint16_t attribute;
    };
    std::vector< Prism > prisms;
    std::vector< Tri > tris;
    for (std::size_t t = 0; t < triCount; t++) {
        const Vec3& a = mesh.positions[mesh.indices[t * 3]];
        const Vec3& b = mesh.positions[mesh.indices[t * 3 + 1]];
        const Vec3& c = mesh.positions[mesh.indices[t * 3 + 2]];
        const V v1 = toV(a), v2 = toV(b), v3 = toV(c);
        const V face = cross(v2 - v1, v3 - v1);
        if (length(face) < 1e-6 || length(v2 - v1) < 1e-4 || length(v3 - v1) < 1e-4 || length(v3 - v2) < 1e-4) {
            stats.degenerate++;
            continue;
        }
        // As DynamicCollisionObj::updateTriangle builds them: the face normal, then the
        // outward normals of the edges v1-v3, v1-v2 and v2-v3; the height is v1's distance
        // to the edge v2-v3 (KCollisionServer::getPos inverts this).
        const V n = normalized(face);
        const V e0 = normalized(cross(n, v3 - v1));
        const V e1 = normalized(cross(v2 - v1, n));
        const V e2 = normalized(cross(v3 - v2, n));
        Prism p;
        p.height = static_cast< float >(dot(v3 - v1, e2));
        const auto key = std::make_tuple(a.x, a.y, a.z);
        auto it = positionIds.find(key);
        if (it == positionIds.end()) {
            it = positionIds.emplace(key, static_cast< std::uint32_t >(positions.size())).first;
            positions.push_back(v1);
        }
        p.pos = it->second;
        p.nrm = normals.add(n);
        p.edge[0] = normals.add(e0);
        p.edge[1] = normals.add(e1);
        p.edge[2] = normals.add(e2);
        const std::uint16_t surface = mesh.triangleSurface.empty() ? 0 : mesh.triangleSurface[t];
        if (surface >= surfaces.size()) {
            result.error = "a triangle's surface is past the surfaces";
            return result;
        }
        p.attribute = surface;
        prisms.push_back(p);
        tris.push_back({{v1 - origin, v2 - origin, v3 - origin}});
    }
    if (prisms.empty()) {
        result.error = "every triangle is degenerate";
        return result;
    }
    if (prisms.size() > 0xFFFF || positions.size() > 0xFFFF || normals.mNormals.size() > 0xFFFF) {
        result.error = "the mesh needs more than 65535 prisms, positions or normals: split it into several parts";
        return result;
    }
    stats.triangles = static_cast< int >(prisms.size());
    stats.positions = static_cast< int >(positions.size());
    stats.normals = static_cast< int >(normals.mNormals.size());

    // The octree.
    const int nx = 1 << axisShift[0], ny = 1 << axisShift[1], nz = 1 << axisShift[2];
    const int topCells = nx * ny * nz;
    std::vector< Node > nodes(topCells);
    OctreeBuilder builder(tris, options);
    const double cell = std::ldexp(1.0, cellShift);
    // Candidates per top-level cell, from each triangle's bounds (the exact test is in build).
    std::vector< std::vector< std::uint16_t > > candidates(topCells);
    const int counts[3] = {nx, ny, nz};
    for (std::size_t i = 0; i < tris.size(); i++) {
        int from[3], to[3];
        for (int a = 0; a < 3; a++) {
            double lo = 1e300, hi = -1e300;
            for (const V& p : tris[i].p) {
                const double c = a == 0 ? p.x : a == 1 ? p.y : p.z;
                lo = std::min(lo, c);
                hi = std::max(hi, c);
            }
            from[a] = std::clamp(static_cast< int >(std::floor((lo - 1.0) / cell)), 0, counts[a] - 1);
            to[a] = std::clamp(static_cast< int >(std::floor((hi + 1.0) / cell)), 0, counts[a] - 1);
        }
        for (int z = from[2]; z <= to[2]; z++) {
            for (int y = from[1]; y <= to[1]; y++) {
                for (int x = from[0]; x <= to[0]; x++) {
                    candidates[(z << (axisShift[0] + axisShift[1])) | (y << axisShift[0]) | x].push_back(static_cast< std::uint16_t >(i + 1));
                }
            }
        }
    }
    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                const int index = (z << (axisShift[0] + axisShift[1])) | (y << axisShift[0]) | x;
                builder.build(V{x * cell, y * cell, z * cell}, cellShift, candidates[index], &nodes, index);
            }
        }
    }
    stats.topCells = topCells;
    for (const Node& node : nodes) {
        (node.leaf ? stats.leaves : stats.branches)++;
    }
    stats.lists = static_cast< int >(builder.mLists.size());
    for (const auto& list : builder.mLists) {
        stats.maxListLength = std::max(stats.maxListLength, static_cast< int >(list.size()));
    }

    // Layout: header, positions, normals, prisms 1..n (prism 0 overlaps the normals' last 16
    // bytes), then the octree: the top-level words, the branch blocks of 8 words in node
    // order, then the u16 lists.
    const std::uint32_t posOffset = 0x38;
    const std::uint32_t nrmOffset = posOffset + static_cast< std::uint32_t >(positions.size()) * 12;
    std::uint32_t nrmCount = static_cast< std::uint32_t >(normals.mNormals.size());
    // The format needs at least 16 bytes of normals for prism 0 to overlap; a triangle
    // always has 4 normals, so this holds (and the reader accepts any count).
    const std::uint32_t prismStart = nrmOffset + nrmCount * 12;
    const std::uint32_t prismOffset = prismStart - 0x10;
    const std::uint32_t octreeOffset = prismStart + static_cast< std::uint32_t >(prisms.size()) * 0x10;

    // Node words: top-level cells occupy the first `topCells` words; each branch's children
    // are a block of 8 words, placed in the order the nodes were made (children indices are
    // consecutive), so a node's word offset is 4 * its index.
    const std::uint32_t wordsSize = static_cast< std::uint32_t >(nodes.size()) * 4;
    std::vector< std::uint32_t > listOffsets(builder.mLists.size());
    std::uint32_t listBytes = 0;
    for (std::size_t i = 0; i < builder.mLists.size(); i++) {
        listOffsets[i] = wordsSize + listBytes;  // the first entry, relative to the octree
        listBytes += static_cast< std::uint32_t >(builder.mLists[i].size() + 1) * 2;
    }
    const std::uint32_t total = octreeOffset + wordsSize + listBytes;
    Bytes& kcl = result.kcl;
    kcl.assign((total + 3) & ~3u, 0);

    put32(kcl, 0x00, posOffset);
    put32(kcl, 0x04, nrmOffset);
    put32(kcl, 0x08, prismOffset);
    put32(kcl, 0x0C, octreeOffset);
    putF(kcl, 0x10, options.thickness);
    putF(kcl, 0x14, static_cast< float >(origin.x));
    putF(kcl, 0x18, static_cast< float >(origin.y));
    putF(kcl, 0x1C, static_cast< float >(origin.z));
    put32(kcl, 0x20, ~((std::uint32_t(nx) << cellShift) - 1));
    put32(kcl, 0x24, ~((std::uint32_t(ny) << cellShift) - 1));
    put32(kcl, 0x28, ~((std::uint32_t(nz) << cellShift) - 1));
    put32(kcl, 0x2C, static_cast< std::uint32_t >(cellShift));
    put32(kcl, 0x30, static_cast< std::uint32_t >(axisShift[0]));
    put32(kcl, 0x34, static_cast< std::uint32_t >(axisShift[0] + axisShift[1]));
    for (std::size_t i = 0; i < positions.size(); i++) {
        putF(kcl, posOffset + i * 12, static_cast< float >(positions[i].x));
        putF(kcl, posOffset + i * 12 + 4, static_cast< float >(positions[i].y));
        putF(kcl, posOffset + i * 12 + 8, static_cast< float >(positions[i].z));
    }
    for (std::size_t i = 0; i < normals.mNormals.size(); i++) {
        const V& n = normals.mNormals[i];
        putF(kcl, nrmOffset + i * 12, static_cast< float >(n.x));
        putF(kcl, nrmOffset + i * 12 + 4, static_cast< float >(n.y));
        putF(kcl, nrmOffset + i * 12 + 8, static_cast< float >(n.z));
    }
    for (std::size_t i = 0; i < prisms.size(); i++) {
        const std::size_t at = prismStart + i * 0x10;
        const Prism& p = prisms[i];
        putF(kcl, at, p.height);
        put16(kcl, at + 4, static_cast< std::uint16_t >(p.pos));
        put16(kcl, at + 6, static_cast< std::uint16_t >(p.nrm));
        put16(kcl, at + 8, static_cast< std::uint16_t >(p.edge[0]));
        put16(kcl, at + 10, static_cast< std::uint16_t >(p.edge[1]));
        put16(kcl, at + 12, static_cast< std::uint16_t >(p.edge[2]));
        put16(kcl, at + 14, p.attribute);
    }
    // A word is relative to its block: the top level for top cells, else the 8-word block
    // holding it. Leaves point at the u16 before their list's first entry.
    std::vector< std::uint32_t > blockOf(nodes.size(), 0);
    for (std::size_t i = 0; i < nodes.size(); i++) {
        if (!nodes[i].leaf) {
            for (int k = 0; k < 8; k++) {
                blockOf[nodes[i].children + k] = static_cast< std::uint32_t >(nodes[i].children) * 4;
            }
        }
    }
    for (std::size_t i = 0; i < nodes.size(); i++) {
        const std::uint32_t base = blockOf[i];
        std::uint32_t word;
        if (nodes[i].leaf) {
            word = 0x80000000u | (listOffsets[nodes[i].list] - 2 - base);
        } else {
            word = static_cast< std::uint32_t >(nodes[i].children) * 4 - base;
        }
        put32(kcl, octreeOffset + i * 4, word);
    }
    for (std::size_t i = 0; i < builder.mLists.size(); i++) {
        std::uint32_t at = octreeOffset + listOffsets[i];
        for (std::uint16_t prism : builder.mLists[i]) {
            put16(kcl, at, prism);
            at += 2;
        }
        put16(kcl, at, 0);
    }

    // The .pa: one row per surface the mesh uses (prism attributes index it).
    result.pa = buildPa(surfaces);
    return result;
}

}  // namespace PetariEditor::Terrain
