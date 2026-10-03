#pragma once
// The level editor's play-test isolation, the game's side (docs/dev/level-editor/TECH.md 2.7):
// the save file's progress as the edit session found it, put back when the session ends, so
// stars, coins and flags a play-test earns never reach the player's records.
#ifdef PETARI_NATIVE
namespace EditorScratch {
    /// Takes the current save file's progress (the game data, mod galaxies' records included).
    void snapshot();
    /// Puts it back (nothing when no snapshot was taken). Game thread.
    void restore();
}  // namespace EditorScratch
#endif
