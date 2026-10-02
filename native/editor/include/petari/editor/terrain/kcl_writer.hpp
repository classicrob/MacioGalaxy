#pragma once
// KCL collision writer (docs/dev/level-editor/TECH.md 6.3): a terrain mesh to the disc's .kcl
// (big-endian, as ObjectData archives hold it) and its .pa attribute table (a BCSV).
//
// The .kcl layout is the one native/include/petari/kcl_collision.hpp documents and the game's
// KCollisionServer reads: positions, normals (face normal then the three edge normals,
// de-duplicated), 16-byte prisms indexed from 1 (prism 0 overlaps the last 16 bytes of the
// normals), and an octree: a top level of 2^n x 2^m x 2^k cells, each a leaf (a 0-terminated
// u16 prism list) or a branch of 8 children, subdivided until a leaf holds at most
// `maxPrismsPerLeaf` prisms or reaches `minCellShift`. Identical lists are shared.
//
// Live use: copy `kcl` to a host buffer, convert it with PetariNative::KCL::convertInPlace and
// hand it to CollisionParts::init, as ResourceHolder does for archive files.
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/terrain/mesh.hpp>
#include <string>

namespace PetariEditor::Terrain {

struct KclOptions {
    float thickness = 40.0f;      // the header's prism thickness (every disc file has 40)
    int maxPrismsPerLeaf = 24;    // subdivide cells with more
    int minCellShift = 6;         // smallest cell: 64 units
    int topCellsPerAxis = 8;      // about this many top-level cells along the longest axis
    float padding = 50.0f;        // the octree's space reaches this far past the mesh bounds
    float normalTolerance = 1e-5f;  // normals closer than this per component are shared
};

struct KclStats {
    int triangles = 0;    // prisms written
    int degenerate = 0;   // zero-area triangles skipped
    int positions = 0;
    int normals = 0;
    int topCells = 0;
    int branches = 0;
    int leaves = 0;
    int lists = 0;        // distinct prism lists
    int maxListLength = 0;
};

struct KclResult {
    Formats::Bytes kcl;
    Formats::Bytes pa;
    KclStats stats;
    std::string error;  // empty on success
};

/// Builds the .kcl and .pa for `mesh`. Fails (error set, buffers empty) when the mesh has no
/// usable triangle, or more positions, normals or prisms than the format's u16 indices hold
/// (split the mesh into several parts, TECH.md 6.3).
KclResult buildKcl(const Mesh& mesh, const KclOptions& options = KclOptions());

/// The .pa table alone: one row per surface, its codes packed in one u32 like the disc's
/// (camera_id bits 0-7, Sound_code 8-14, Floor_code 15-20, Wall_code 21-24, Camera_through 25).
Formats::Bytes buildPa(const std::vector< Surface >& surfaces);

}  // namespace PetariEditor::Terrain
