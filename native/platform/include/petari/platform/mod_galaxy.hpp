#pragma once
// The mod galaxy registry (docs/dev/level-editor/TECH.md 5.7): what the executable hard-codes for
// every disc galaxy (names in Message.arc, GalaxyID.bcsv rows, event flags, music), supplied for
// the new galaxies of enabled folder mods. Each mod may hold a galaxy.json beside its files/:
//
//   { "format": 1,
//     "galaxies": [ {
//       "name": "ModTestGalaxy",                       // the StageData name (archives, ScenarioData)
//       "title": { "en": "Blockout Test Galaxy", "fr": "...", "es": "..." },   // GalaxyName_<name>
//       "shortTitle": { "en": "Blockout Test" },       // GalaxyNameShort_<name> (default: title)
//       "missions": [ { "en": "The Checkered Box" } ], // ScenarioName_<name><n>, n from 1
//       "music": "EggStarGalaxy"                       // a disc galaxy whose music and sound banks it borrows
//     } ] }
//
// Text is UTF-8 in the file and answered as UTF-16 (the game's message text); a language missing
// falls back to "en". Mods never replace Message.arc for this, so they do not conflict.
// Pure C++, no game code: the game's hooks (PETARI_NATIVE) ask it.
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace PetariNative::Platform::ModGalaxy {

struct Galaxy {
    std::string name;
    std::string mod;    // the folder mod that supplies it
    std::string music;  // borrowed music ("" = none)
    std::map< std::string, std::u16string > title;       // language ("en") -> text
    std::map< std::string, std::u16string > shortTitle;
    std::vector< std::map< std::string, std::u16string > > missions;
    bool rejected = false;  // dropped after load (e.g. a save-hash collision); see reject()
};

/// Reads <root>/galaxy.json of each enabled mod root (in precedence order: a name already
/// registered by an earlier mod is skipped). Log lines describe what was registered or refused.
void load(const std::vector< std::filesystem::path >& modRoots, std::vector< std::string >* pLog);
/// One galaxy.json's text (tests, and load()). False and *pError: nothing registered from it.
bool loadText(const std::string& json, const std::string& mod, std::string* pError);
/// The same, for galaxies this mod already registered: their names, missions and music are
/// replaced in place (the level editor's Missions panel). Earlier message() pointers stay valid.
bool update(const std::string& json, const std::string& mod, std::string* pError);
void clear();

/// The registered galaxy of that StageData name (exact, case-sensitive like the game's tables),
/// or null. Rejected galaxies are not found.
const Galaxy* find(const char* pName);
std::vector< const Galaxy* > all();

/// The text of a message id the registry answers: "GalaxyName_<G>", "GalaxyNameShort_<G>",
/// "ScenarioName_<G><n>" (n from 1), in `language` ("en", "fr", "es"...; English fallback; null:
/// the game's language from the provider). Null for any other id. The pointer stays valid for the
/// process.
const char16_t* message(const char* pId, const char* pLanguage = nullptr);

/// The game's current language as a code ("en"), for message(); set by the game
/// (src/Game/System/Language.cpp). Without one, English.
void setLanguageProvider(const char* (*pProvider)());

/// The disc galaxy whose music `pGalaxy` borrows, or null.
const char* musicFrom(const char* pGalaxy);

/// Drops a registered galaxy for the rest of the run, with a reason for the log.
void reject(const char* pName, const char* pReason);

// ---- The save sidecar (TECH.md 5.7) ----
// GameData.bin keeps only the disc's galaxies (vanilla-shaped, so turning a mod off loses
// nothing and another tool reads it). A mod galaxy's per-file record (the GALA record: Power Star
// bits, visited bits, best coins per mission) is kept in <user>/mods-saves/<Mod>.json:
//   { "format": 1, "files": { "mario1": { "ModTestGalaxy": { "stars": 1, "visited": 1,
//                                                          "coins": [0, 0, 0, 0, 0, 0, 0, 0] } } } }

struct GalaxyRecord {
    std::string galaxy;
    std::uint8_t stars = 0;    // bit n-1: Power Star n owned
    std::uint8_t visited = 0;  // bit n-1: mission n played
    std::uint16_t coins[8] = {};
};

/// Where the sidecars live (<user>/mods-saves); empty (the default): nothing is saved or loaded.
void setSaveDirectory(const std::filesystem::path& directory);
/// Writes the records of the registered galaxies for one user file ("mario1"), each into its mod's
/// sidecar, keeping the other user files' entries. Records of unregistered galaxies are ignored.
bool saveRecords(const std::string& userFile, const std::vector< GalaxyRecord >& records, std::string* pError);
/// The registered galaxies' records for one user file (none when there is no sidecar).
std::vector< GalaxyRecord > loadRecords(const std::string& userFile);

/// The save's 16-bit galaxy key (JGadget hash, h = c + 31 h over signed chars, truncated): mod
/// galaxies whose key equals a disc galaxy's would share its save record, so they are rejected.
std::uint16_t saveKey(const char* pName);

}  // namespace PetariNative::Platform::ModGalaxy
