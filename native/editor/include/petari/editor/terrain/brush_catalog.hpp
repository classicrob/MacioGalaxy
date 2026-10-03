#pragma once
// The terrain brush in the editor's catalog and Checks (docs/dev/level-editor/TECH.md 6.5), from
// rows alone: no runtime or game, so the in-game editor (TerrainSync) and the headless editor
// register the same class and checks.
#include <petari/editor/catalog.hpp>
#include <petari/editor/edit_api.hpp>
#include <vector>

namespace PetariEditor::Terrain {

/// The class "EditorBrush": Browser presets (one per shape) and its four args as named Enum args
/// (Shape, Material, Surface, Gravity).
Catalog::ClassInfo brushClassInfo();
/// Catalog::addClass(brushClassInfo()). After every catalog load (a load replaces added classes).
void registerBrushClass();

/// The brush rows' checks: "brush-tris:<row hex>" warns when a brush's mesh has more triangles
/// than a collision file holds (about 16,000).
void brushChecks(const ModelView& model, std::vector< Check >& out);
/// registerCheckProvider(brushChecks), once.
void registerBrushChecks();

}  // namespace PetariEditor::Terrain
