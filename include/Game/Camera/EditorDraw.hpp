#pragma once

#ifdef PETARI_NATIVE
#include <cstdint>

// The level editor's drawing in the scene (docs/dev/level-editor/TECH.md 3.1 and 3.4): the selection
// and the hovered object, drawn again over themselves in the selection's gold and the hover's cyan,
// with a faint x-ray where something is in front of them. Called at the end of GameScene::draw3D,
// before the Z buffer is cleared, only in Edit mode.
class J3DModel;

namespace EditorDraw {
    void draw3D();

    // The editor's extra model draws share one budget a frame (EP-004): Aurora's index buffer is a fixed
    // 2 MiB a frame, and a huge planet outlined (10 passes) overflowed it and aborted. Estimated from the
    // model's display lists; resets each frame (beginFrame, at the start of GameScene::draw3D).
    void beginFrame();
    std::uint32_t modelCost(J3DModel* model);       // estimated index bytes for one draw of the model
    bool spend(J3DModel* model, int passes);         // true (and charged) if `passes` draws fit
    bool spendBytes(std::uint32_t indexBytes);       // the same for raw geometry (DebugDraw batches, PiP passes)
    std::uint32_t remaining();                       // what is left this frame
}  // namespace EditorDraw
#endif
