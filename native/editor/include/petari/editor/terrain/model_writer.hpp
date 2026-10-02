#pragma once
// J3D model writer for terrain (docs/dev/level-editor/TECH.md 6.3): a mesh to a static model the
// game loads like any planet's, with one joint, one shape and one material cloned from a model
// on the player's own disc (so a shared mod carries a reference, never the material or texture).
//
// The file is J3D2bmd3 (.bmd): INF1, VTX1, EVP1, DRW1, JNT1, SHP1, MAT3, TEX1. The game takes
// .bmd wherever it takes .bdl (ResourceHolder's model extensions), and a .bmd needs no MDL3
// (precompiled material display lists): J3D builds them from MAT3 at load.
//
// Vertices: POS (f32 xyz), NRM (f32 xyz), CLR0 (one white RGBA8 entry), TEX0 (f32 st), each
// indexed with u16; triangles as GX_DRAW_TRIANGLES, split every 65,535 vertices.
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/terrain/mesh.hpp>
#include <optional>
#include <string>
#include <vector>

namespace PetariEditor::Terrain {

/// One material cut out of a donor model: a MAT3 block holding just that material (its shared
/// pools copied whole, so its indices stay valid) and the donor's TEX1 block.
struct MaterialClone {
    std::string name;
    Formats::Bytes mat3;
    Formats::Bytes tex1;
};

/// The material names of a .bdl/.bmd (big-endian, as on the disc), in order; empty when it is
/// not a J3D2 model with a MAT3 block.
std::vector< std::string > modelMaterialNames(const Formats::Bytes& model);

/// Clones `materialName` out of `donorModel`. Nullopt and *pError when the model or material is
/// missing or malformed.
std::optional< MaterialClone > cloneMaterial(const Formats::Bytes& donorModel, const std::string& materialName, std::string* pError);

struct ModelResult {
    Formats::Bytes model;  // big-endian J3D2bmd3
    std::string error;     // empty on success
    int vertices = 0;
    int triangles = 0;
};

/// Builds the model. The mesh needs normals and UVs per position (the brushes make them).
ModelResult buildModel(const Mesh& mesh, const MaterialClone& material, const std::string& jointName = "root");

}  // namespace PetariEditor::Terrain
