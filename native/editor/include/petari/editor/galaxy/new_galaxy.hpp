#pragma once
// A new galaxy's files from a template (docs/dev/level-editor/new-galaxy-flow.md, UX.md 14,
// TECH.md 5.7): the galaxy's own stage archive, its Scenario archive and the registry entry
// (galaxy.json). Pure C++ over the formats library; the editor's New Galaxy command and the
// headless tools call it. Nothing here touches the disc but through the readers.
#include <petari/editor/formats/formats.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace PetariEditor::Galaxy {

/// Reads a disc file ("StageData/EggStarGalaxy.arc", "ObjectData/PlanetMapDataTable.arc"; Yaz0
/// or plain); nullopt when missing.
using DiscReader = std::function< std::optional< Formats::Bytes >(const std::string& path) >;
/// The names in a disc directory ("StageData" -> "EggStarGalaxy.arc", "EggStarGalaxy", ...).
using DiscLister = std::function< std::vector< std::string >(const std::string& directory) >;

enum class Template {
    Empty,      // a start point in empty space
    OnePlanet,  // one disc planet under the start, with its gravity
    Brushes,    // a checker box brush under the start, fitted gravity
    Copy,       // a disc galaxy's own stage and scenarios, renamed (its zones stay shared)
};

struct NewGalaxySpec {
    std::string name;          // "Pipe Dream" (English; the other languages default to it)
    std::string internalName;  // empty: internalNameFor(name)
    Template from = Template::OnePlanet;
    std::string planet;        // OnePlanet: a PlanetMapDataTable name ("EggStarPlanet"...)
    std::string copyOf;        // Copy: a disc galaxy ("EggStarGalaxy")
    std::string musicFrom;     // a disc galaxy whose music and sound banks it borrows; empty: copyOf or "EggStarGalaxy"
    std::vector< std::string > missions;  // names; their count is the mission count (1..8); empty: one, "Mission 1"
};

struct NewGalaxyFiles {
    std::string internalName;
    /// Below the mod's files/: "StageData/PipeDreamGalaxy.arc", "StageData/PipeDreamGalaxy/PipeDreamGalaxyScenario.arc".
    std::map< std::string, Formats::Bytes > files;
    std::string galaxyJson;  // <mod>/galaxy.json
    std::vector< std::string > log;
    std::string error;  // empty on success
};

/// "Pipe Dream" -> "PipeDreamGalaxy": letters and digits, words capitalised, "Galaxy" appended
/// unless the name ends with it.
std::string internalNameFor(const std::string& name);

/// Why `internalName` can't be used ("" when it can): characters, length, an existing disc
/// galaxy or zone, or one of `taken` (the registry's galaxies).
std::string checkInternalName(const std::string& internalName, const DiscLister& listDisc, const std::vector< std::string >& taken);

/// The disc's galaxies (StageData/<G>/<G>Scenario.arc exists), sorted.
std::vector< std::string > discGalaxies(const DiscLister& listDisc);
/// The planets the game can place by name (PlanetMapDataTable), sorted.
std::vector< std::string > planetNames(const DiscReader& readDisc);

NewGalaxyFiles buildNewGalaxy(const NewGalaxySpec& spec, const DiscReader& readDisc, const DiscLister& listDisc);

}  // namespace PetariEditor::Galaxy
