#pragma once

#ifdef PETARI_NATIVE
#include "Game/Util/JMapInfo.hpp"
#include <revolution.h>

class LiveActor;
class NameObj;

// The level editor's game-side operations on a running scene (docs/dev/level-editor/TECH.md
// 2.2, 2.3). Native only; called by the editor runtime with the CPU baton held.
namespace EditorScene {
    struct Created {
        NameObj* mObj = nullptr;
        LiveActor* mFirstActor = nullptr;  // the first LiveActor the creation registered (the object itself, usually)
        s32 mActorCount = 0;               // LiveActors registered (children included)
        s32 mFirstIndex = -1;              // their index in AllLiveActorGroup (consecutive)
    };
    /// A live actor by its AllLiveActorGroup index.
    LiveActor* actorAt(s32 index);

    /// Creates the object a placement row describes, in a scene that has finished loading:
    /// mounts its archives, creates and initialises it as placement does, and connects it to
    /// the scene's lists. The row's memory must stay valid while the object lives and be
    /// registered with PetariEditor::Hooks::registerRows for `zoneId` (unless it is inside a
    /// loaded archive). False and *ppError: nothing was created.
    bool createFromRow(const JMapInfoIter& rIter, Created* pOut, const char** ppError);

    /// Removes an actor the editor created or replaced: dead, hidden, out of clipping. Its
    /// memory stays (nothing is freed mid-scene; TECH.md 2.3).
    void retire(LiveActor* pActor);

    /// Live actors and the limit, for capacity checks.
    s32 liveActorCount();
    s32 liveActorLimit();
    s32 gravityCount();  // registered gravity fields (limit 128, no unregister)

    /// The current stage and mission ("EggStarGalaxy", 1); null when no stage is loaded.
    const char* stageName();
    s32 scenario();

    /// The loaded data of a table of a zone's archive ("jmp/placement/layera/objinfo"), or null
    /// when the zone is not placed or has no such table. Used to bind placed rows to the model.
    const void* tableData(s32 zoneId, const char* pPath);

    /// Puts an actor where a row with this zone-local transform would place it (getJMapInfoTrans /
    /// getJMapInfoRotate), and updates its matrices at once, so a frozen scene shows it.
    void applyTransform(LiveActor* pActor, s32 zoneId, const f32* pPos, const f32* pDirDegrees, const f32* pScale);

    /// The scene heap's free and total bytes (the memory meter).
    void sceneHeap(u32* pFree, u32* pTotal);
}  // namespace EditorScene

// The engine-hooks live check (src/Game/Editor/EditorHooksTest.cpp); GameScene::update calls it.
namespace EditorHooksTest {
    void update(bool actionNerve);
}
#endif
