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
    };

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
}  // namespace EditorScene

// The engine-hooks live check (src/Game/Editor/EditorHooksTest.cpp); GameScene::update calls it.
namespace EditorHooksTest {
    void update(bool actionNerve);
}
#endif
