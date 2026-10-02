#pragma once
// Blockout brushes (docs/dev/level-editor/TECH.md 6.4, UX section 15): a brush is an ordinary
// placement row of the reserved class "EditorBrush", so undo, the journal, layers, the gizmo
// and saving work on it unchanged:
//
//   pos_*      the brush's bottom centre        dir_*   its rotation (degrees)
//   scale_*    its size / 1000: box width, height, depth; sphere diameter (x); cylinder
//              diameter (x) and height (y); ramp width, height, depth (it rises towards +z)
//   Obj_arg0   shape (BrushShape)               Obj_arg1  material (materialPalette() index)
//   Obj_arg2   surface (surfacePresets() index) Obj_arg3  gravity (0: the stage's, 1: fitted)
//
// The editor shows a brush live from these values (collision from an in-memory KCL, the model
// from an in-memory archive). Saving bakes each one into a generated planet: an ObjectData
// archive with the model and collision, a PlanetMapDataTable row and, when fitted, a gravity row.
// Pure C++ (no game code).
#include <petari/editor/terrain/mesh.hpp>
#include <string>
#include <vector>

namespace PetariEditor::Terrain {

/// The placement name of brush rows.
inline constexpr const char* kBrushClass = "EditorBrush";

enum class BrushShape : int { Box = 0, Sphere = 1, Cylinder = 2, Ramp = 3 };
const std::vector< std::string >& brushShapeNames();  // "Box", "Sphere", "Cylinder", "Ramp"

/// A look from the player's own disc (a planet's material), cloned into the brush's model.
struct MaterialPreset {
    std::string label;          // "Grass"
    std::string donorArchive;   // "FlowerPlanet" (ObjectData/<donorArchive>.arc)
    std::string donorModel;     // "flowerplanet.bdl"
    std::string material;       // "gras_v"
    int sound = 0;              // Sound_code the surface gets with this look ("Lawn")
};
const std::vector< MaterialPreset >& materialPalette();

/// Collision behaviour (Floor_code / Wall_code), whatever the look.
struct SurfacePreset {
    std::string label;  // "Ice"
    int floor = 0;
    int wall = 0;
};
const std::vector< SurfacePreset >& surfacePresets();

struct Brush {
    BrushShape shape = BrushShape::Box;
    Vec3 size{1000, 1000, 1000};  // world units (scale * 1000), clamped to a minimum
    int material = 0;
    int surface = 0;
    bool fittedGravity = true;
};

/// A brush from its row's values (arg -1 means the default: box, the first material, normal,
/// fitted gravity). Out-of-range values clamp.
Brush brushFromRow(int arg0, int arg1, int arg2, int arg3, const Vec3& scale);

/// The brush's mesh in its own space: bottom centre at the origin, +y up, with normals, UVs
/// (texture lock, world-aligned) and the surface on every triangle.
Mesh brushMesh(const Brush& brush);

/// A gravity field fitted to the brush, in the brush's own space (TECH.md 6.3):
///   box -> Cube (every face); sphere -> Point; cylinder -> Segment along the axis;
///   ramp -> Cube over its bounds with only the top face (gravity straight down onto it).
struct GravityFit {
    enum class Kind { None, Cube, Point, Segment } kind = Kind::None;
    Vec3 center;            // Cube, Point
    Vec3 half;              // Cube half extents
    unsigned activeFaces = 0x3F;  // Cube faces (CubeGravity::mActiveFaces bits)
    float radius = 0.0f;    // Point, Segment: distance from the centre/axis to the surface
    Vec3 a, b;              // Segment ends
    float range = 0.0f;     // how far past the surface it pulls
};
GravityFit fitGravity(const Brush& brush);

/// A cylinder with its bottom centre at `base` (segments >= 3), and a ramp (a wedge rising from
/// the front edge, z = -d/2, to the back, z = +d/2), both with UVs per face.
Mesh makeCylinder(const Vec3& base, float radius, float height, int segments, const Surface& surface = Surface(),
                  float uvScale = 500.0f);
Mesh makeRamp(const Vec3& base, const Vec3& size, const Surface& surface = Surface(), float uvScale = 500.0f);

}  // namespace PetariEditor::Terrain
