#pragma once
#ifdef PETARI_NATIVE
#include "Game/Camera/EditorPicking.hpp"
#include <petari/editor/gravity.hpp>
class LiveActor;
namespace EditorGravity {
    void update(const EditorPicking::View& view);
    void draw();
    // Game thread / CPU baton held; independent of gravity-view visibility.
    PetariEditor::Gravity::Probe query(PetariEditor::Vec3 position);
    PetariEditor::Gravity::Probe queryGround(PetariEditor::Vec3 origin, PetariEditor::Vec3 direction);
    void testTick(bool playable);
    PetariEditor::RowId pick(PetariEditor::Vec3 origin, PetariEditor::Vec3 direction, float* distance);
    bool bounds(const LiveActor* actor, float center[3], float* radius);
}  // namespace EditorGravity
#endif
