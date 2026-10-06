#pragma once
// Normal-play direct galaxy entry (--stage <galaxy> --scenario <n>, native/README.md).
//
// Unlike the test fixture (test_fixture.hpp), this changes no progression: the
// player picks a file as usual, and the first file load of the session goes to
// this galaxy and mission instead of the observatory. Applied once, and only to
// a file past the tutorial (the observatory is open); otherwise the game's own
// destination is kept. Everything after that, including saving stars, is the
// game's own logic.

#include <atomic>
#include <string>

namespace PetariNative::PlayerLaunch {
// Internal stage name (e.g. "EggStarGalaxy"); empty: not in use. Set by the CLI
// before the game starts, cleared by the game once the jump is taken or refused.
inline std::string stage;
inline int scenario = 0;

// A restart into a new galaxy (the level editor's New Galaxy: PETARI_OPEN_* from the previous
// process, read once by app_main). The game skips the strap's wait and the title and opens file
// `openFile` (1-based; 0: off) without asking, then goes to `stage` as above; the editor enters
// Edit there (openEdit). openTitle is the galaxy's name, for the "Creating" screen.
inline int openFile = 0;
inline bool openEdit = false;
inline std::string openTitle;

// The Home menu's Level Select (native/LEVEL_SELECT.md): a warp the game
// starts at its next normal gameplay frame (GameScene::update), through the
// game's own galaxy move, and then clears. Game threads only.
inline char warpStage[64] = {};
inline int warpScenario = 0;
inline std::atomic<bool> warpPending{false};
// M4: Home closes before the UI opens the New Galaxy sheet; never a warp destination.
inline std::atomic<bool> newGalaxyPending{false};
}  // namespace PetariNative::PlayerLaunch
