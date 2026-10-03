#pragma once
// The level editor runtime's entry point in the frame (TECH.md 2.10, 10).

#include <petari/editor/edit_api.hpp>

#include <cstdint>

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

/// Loads the edit model for the current stage and mission and binds the scene, without entering
/// Edit, and keeps it loaded (across reloads and stage changes) for the rest of the run: model()
/// is then non-null in Play, and enqueue()d commands apply on the next tick with SceneSync
/// re-creating actors in the running scene (headless/agent tools through the test server). Game
/// thread. False: the editor is off, no stage is loaded, or the model couldn't load
/// (status().lastError says why).
bool ensureModel();

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

// ---- The play-test loop (UX 3, TECH 2.7). Game thread. -------------------------------------------

enum class PlayEvent : std::uint8_t {
    Begin,   // a new run: Play from the marker or the mission's start (reset trail, timer, scratch progress)
    Resume,  // Play continued where Edit left the scene (keep the run's state)
    ToEdit,  // back in Edit after a run (Tab, a star caught, a miss)
    End,     // the edit session ends: another galaxy or mission, or the editor turned off
};
/// Told after the mode change, with the session's galaxy and mission.
using PlayListener = void (*)(PlayEvent event, const char* galaxy, int mission);
void registerPlayListener(PlayListener listener);
/// Back to Edit now (a play-test star caught): `reason` becomes the notice ("Cleared in 0:42").
/// False outside Play.
bool requestEdit(const char* reason);
/// True while an edit session exists (its model loaded, until End): progress made now is a
/// play-test's and must not reach the player's real records. The save goes to a scratch copy,
/// in-memory progress is restored at End, and other progress writers (progress.json, the mod
/// galaxies' sidecars) skip their writes while this is true. Any thread.
bool progressIsolated();
/// GameScene::requestEndMissDemo: during a play-test a miss returns to Edit (an editor reload; no
/// lives lost) instead of the game's restart. False (the game's own path) outside Play.
bool interceptMiss();

/// Recomputes the Checks on the next tick without changing their baseline (a check provider's
/// live inputs changed: gravity samples).
void invalidateChecks();

}  // namespace PetariEditor::Runtime
