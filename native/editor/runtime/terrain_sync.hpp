#pragma once
// SceneSync for terrain brushes (petari/editor/terrain/brush.hpp): rows of the class
// EditorBrush are shown by live terrain parts (src/Game/Editor/EditorTerrain.cpp) instead of the
// game's factory, which has no such class (placement skips them). Seam only, baton held.
#include <petari/editor/edit_api.hpp>
#include <cstddef>

class J3DModel;

namespace PetariEditor {
class EditModel;
}

namespace PetariEditor::TerrainSync {

/// Registers the brush class with the catalog (Browser presets, the Inspector's named args) and
/// its Checks. Called after every catalog load.
void registerWithEditor();

/// True for the brush class name.
bool isBrush(const char* pName);

/// Shows (or updates) the brush of row `id` in the scene: a transform-only change moves its
/// part; a shape, size, material or surface change rebuilds its collision and model; the
/// fitted gravity follows. Binds the part to the row (picking, the gizmo). Errors go to the
/// toolbar (Core::reportError).
void syncBrush(const EditModel& model, RowId id);

/// Removes the brush of row `id` from the scene (deleted, or out of the mission's layers).
void retireBrush(RowId id);

/// After every scene build (SceneSync's bindPlacements): forgets the previous scene's parts and
/// creates every brush row of the placed zones' active layers.
void sceneBound(const EditModel& model);

/// The brush's triangle count (0: not a live brush), for the Checks (a KCL holds about 16k).
std::size_t triangleCount(RowId id);
/// The brush's model (the Browser's thumbnail), or null.
J3DModel* modelForRow(RowId id);

}  // namespace PetariEditor::TerrainSync
