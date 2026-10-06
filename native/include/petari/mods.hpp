#pragma once
// Native gameplay mods (native/MODS.md). Every mod is off unless the player
// turns it on, so an unmodified game and every test run are untouched.
//
// Settings live in mods.txt in the user directory, one "Name=on|off" line per
// mod; a mod with no line has its default (off, except OdysseyJumpDive).
// PETARI_MODS=<name>,<name> (or "none") overrides the file for a run: the
// listed mods on, the others at their default; "-<name>" turns one off.
// The F1 menu's Mods page toggles them and saves the file.

#include <filesystem>
#include <string>

namespace PetariNative::Mods {

enum class Mod : int {
    CollectStarBits,  // one button collects every Star Bit on screen
    ShootEnemy,       // one button fires a Star Bit at the nearest enemy on screen
    OdysseyMovement,  // Mario moves with Super Mario Odyssey's physics (docs/dev/ODYSSEY_MOVEMENT.md); no button
    Cappy,            // Odyssey's cap throw and cap bounce (docs/dev/CAPPY.md); needs OdysseyMovement
    OdysseyJumpDive,  // with OdysseyMovement: pressing Jump in the air dives; ON unless turned off
    SkipCutscenes,    // hold A (Space) 1.5 s to skip cutscenes and dialogue (petari/skip.hpp); no button of its own
    PointToMove,      // hold left click: Mario runs toward the pointer (petari/point_move.hpp); no button of its own
    GenericCharacter, // the player is drawn as a plain figure, not Mario (Game/Player/GenericFigure.hpp); no button
    GenericWorld,     // plain terrain, sky and object stand-ins (Game/System/GenericWorld.hpp); no button
    HideHud,          // no HUD (counters, meters, pointer); PETARI_HIDE_HUD=1 forces it on (LayoutActor.cpp); no button
    LevelEditor,      // the level editor: a Tab tap enters Edit mode (docs/dev/level-editor/UX.md); off by default
    Count
};

const char* name(Mod mod);          // "CollectStarBits", the mods.txt key
const char* description(Mod mod);   // "Collect visible Star Bits", for the menu

bool enabled(Mod mod);  // any thread
void setEnabled(Mod mod, bool on);

// Reads mods.txt (a missing file leaves every mod off), then PETARI_MODS.
// Unknown names or values are an error; nothing changes on error.
bool load(const std::filesystem::path& file, std::string* error);
// Writes every mod's state to the file load() read (no-op before load).
bool save(std::string* error);
// Text form used by load/save.
std::string serialize();
bool parse(const std::string& text, std::string* error);

// Tests only: every mod off, no file.
void resetForTesting();

}  // namespace PetariNative::Mods

// Game side (SDK code, native builds). A press is consumed each call; while a
// mod is off its presses are dropped, so turning it on never fires a stale press.
extern "C" bool petari_mod_take_press(int mod);
extern "C" bool petari_mod_enabled(int mod);
// Whether an input bound to the mod's button is held now (false while the mod is off).
extern "C" bool petari_mod_held(int mod);
