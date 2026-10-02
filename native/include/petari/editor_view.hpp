// The level editor's viewport shape, shared by the presenter and the game's camera
// (docs/dev/level-editor/TECH.md 3.6). Header-only: any library may include it.
#pragma once

#include <atomic>

namespace PetariNative::EditorView {

// While the editor shows the game in a docked viewport of another shape, the game's
// projection takes that viewport's aspect (CameraContext::getAspect), so the image fills
// the viewport instead of being letterboxed. 0 = the game's own aspect.
inline std::atomic<float> gAspect{0.0f};

inline float aspect() {
    return gAspect.load(std::memory_order_relaxed);
}

}  // namespace PetariNative::EditorView
