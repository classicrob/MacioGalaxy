#pragma once
// Terrain meshes for the level editor (docs/dev/level-editor/TECH.md section 6): an indexed
// triangle mesh with a surface per triangle, and the blockout brush primitives. Pure C++ (no
// game code); the KCL and BDL writers take these meshes.
//
// Winding: counter-clockwise seen from the front (the face normal is (v1 - v0) x (v2 - v0)),
// as the game's KCL triangles and J3D front faces are.
#include <cstdint>
#include <string>
#include <vector>

namespace PetariEditor::Terrain {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

/// A triangle's collision attributes: one row of the .pa table. Codes are the game's enum
/// values (CollisionFloorCode etc., include/Game/Map/CollisionCode.hpp); floorCode() and the
/// others map the names the editor shows ("Ice", "Death") to them.
struct Surface {
    int floor = 0;          // Floor_code (Normal)
    int wall = 0;           // Wall_code (Normal)
    int sound = 0;          // Sound_code (null)
    int cameraThrough = 0;  // Camera_through (NoThrough)
    int cameraId = 0xFF;    // camera_id (none, as on the disc)
    bool operator==(const Surface& o) const {
        return floor == o.floor && wall == o.wall && sound == o.sound && cameraThrough == o.cameraThrough && cameraId == o.cameraId;
    }
};

/// Code by name (as the game's CollisionCode tables spell them), or -1.
int floorCode(const std::string& name);
int wallCode(const std::string& name);
int soundCode(const std::string& name);
/// Names by code (for the inspector); empty when out of range.
const std::vector< std::string >& floorCodeNames();
const std::vector< std::string >& wallCodeNames();
const std::vector< std::string >& soundCodeNames();

struct Mesh {
    std::vector< Vec3 > positions;
    std::vector< Vec3 > normals;    // per vertex, for rendering (may be empty)
    std::vector< float > uvs;       // per vertex u, v pairs, for rendering (may be empty)
    std::vector< std::uint32_t > indices;  // 3 per triangle
    std::vector< std::uint16_t > triangleSurface;  // per triangle, an index into surfaces (empty: all 0)
    std::vector< Surface > surfaces;               // at least one when triangleSurface is used

    std::size_t triangleCount() const { return indices.size() / 3; }
    /// Appends another mesh (indices and surface indices are rebased).
    void append(const Mesh& other);
    void bounds(Vec3* pMin, Vec3* pMax) const;
};

/// An axis-aligned box centred on `center` with half extents `half`; 12 triangles, faces
/// split by normal (24 vertices) so each face can be textured. UVs: world-aligned planar
/// projection per face (texture lock), one unit of UV per `uvScale` world units.
Mesh makeBox(const Vec3& center, const Vec3& half, const Surface& surface = Surface(), float uvScale = 500.0f);

/// A UV sphere: `rings` latitude bands (>= 2) and `segments` longitude steps (>= 3).
Mesh makeSphere(const Vec3& center, float radius, int rings, int segments, const Surface& surface = Surface(),
                float uvScale = 500.0f);

}  // namespace PetariEditor::Terrain
