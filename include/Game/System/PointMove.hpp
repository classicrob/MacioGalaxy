#pragma once

#ifdef PETARI_NATIVE
// Point to move mod (native/MODS.md "Point to move"; the input side is
// petari/point_move.hpp): while the player is in normal control, the stick
// toward the Star Pointer from Mario's place on the screen.
namespace PointMove {
    // Game thread, once per frame after the scene update.
    void update();
}  // namespace PointMove
#endif
