#pragma once
// The level editor runtime's entry point in the frame (TECH.md 2.10, 10).

#include <petari/editor/edit_api.hpp>

class LiveActor;
class JMapInfoIter;

namespace PetariEditor::Runtime {

/// GameScene::update, every frame, before the scene's nerves (CPU baton held). Loads the
/// EditModel when an edit session starts, binds placed actors to their rows after each scene
/// build, drains the UI's commands into the model and SceneSync, refreshes status() and runs
/// the UI seam hook. Does nothing while the editor is off.
/// playable: the scene is in normal play where the pause menu could open (a reload is complete
/// and Mario can be moved).
void tick(bool playable);

/// A same-stage rebuild with the editor's fast path (no lives counter, shader gate or wipe;
/// ~0.3-0.5 s): with an edit session in Edit mode it is the editor's Reload (edits included, back
/// in Edit after); without one, a plain restart of the stage (for test drivers). fromStart: at the
/// mission's start instead of the restart point. False when a reload is already in flight or no
/// stage is loaded. Game thread (GameScene::update).
bool reloadScene(bool fromStart);
/// True from reloadScene() (or the editor's Reload) until the new scene is playable.
bool reloading();
/// Current editor play marker, in world space.
bool playMarker(Vec3* position);

// ---- SceneSync extensions (M3: gravity, paths). Register once at start-up; game thread. ----------

/// A drag preview for classes whose look isn't just the actor's transform (a gravity field's
/// shape is baked at init). Asked in order before the generic poke, for Preview and EndPreview
/// (`t` in the row's zone space); true: handled, the generic poke is skipped.
using PreviewHandler = bool (*)(LiveActor* pActor, int zoneId, const RowTransform& t);
void registerPreviewHandler(PreviewHandler handler);

/// An in-place update instead of a re-create (gravity: no new manager slot). Asked in order when a
/// row with a live actor changed, with the new row; true: the actor now matches the row (it stays
/// bound); false: re-create it.
using LiveUpdater = bool (*)(RowId row, LiveActor* pCurrent, const JMapInfoIter& newRow);
void registerLiveUpdater(LiveUpdater updater);

// Non-actor placements (areas). Row handlers run before any LiveActor lookup.
using RowPreviewHandler = bool (*)(RowId row, int zoneId, const RowTransform& transform);
using RowUpdater = bool (*)(RowId row, const JMapInfoIter& newRow);
void registerRowPreviewHandler(RowPreviewHandler handler);
void registerRowUpdater(RowUpdater updater);
// Resolve an engine placement table/ordinal to this scene's current model row.
RowId rowForPlacement(const void* table, int index);

/// Recomputes the Checks on the next tick without changing their baseline (a check provider's
/// live inputs changed: gravity samples).
void invalidateChecks();

}  // namespace PetariEditor::Runtime
