#pragma once
// The object catalog (docs/dev/level-editor/TECH.md 2.11): what every placeable class is, in
// words a person reads, and what it needs. Built from the decompilation (which args each class
// reads, its switches and links), the class sweep (which classes crash without a link) and hand
// curation (names and help). Read-only data; any thread.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace PetariEditor::Catalog {

enum class Category : std::uint8_t {
    Enemy,
    Item,      // coins, Star Bits, 1-Ups, power-ups
    Platform,  // moving and breakable map parts
    Planet,    // planets and terrain pieces
    Gravity,
    Area,      // invisible volumes
    Camera,
    Npc,
    Launcher,  // Launch Stars, Sling Stars, cannons, pipes
    Decoration,
    Logic,     // switches, timers, demos, groups
    Other,
};
const char* categoryName(Category category);  // "Enemies", ...

enum class ArgType : std::uint8_t { Int, Float, Bool, Enum };

struct EnumChoice {
    std::int32_t value = 0;
    std::string label;  // "Walks", "Sleeps until Mario is near"
};

struct Arg {
    int index = 0;      // Obj_arg<index>
    std::string name;   // "Coins inside"; empty: not curated yet (show "Obj_arg3")
    ArgType type = ArgType::Int;
    std::int32_t defaultValue = -1;  // what the class uses when the field is -1 or absent
    std::vector< EnumChoice > choices;  // ArgType::Enum
    std::string unit;   // "frames", "units", "degrees"
    std::string help;   // one line
};

enum class SwitchRole : std::uint8_t {
    Read,   // the object reacts to it (SW_APPEAR, SW_A as a trigger)
    Write,  // the object turns it on (SW_DEAD, SW_A as an output)
};

struct Switch {
    std::string field;  // "SW_APPEAR", "SW_A"
    SwitchRole role = SwitchRole::Read;
    std::string help;   // "appears when this turns on"
};

enum class LinkKind : std::uint8_t { Rail, Group, Camera, Message, Child, Demo, Cast };

struct Link {
    LinkKind kind = LinkKind::Rail;
    std::string field;  // "CommonPath_ID", "GroupId", "CameraSetId", "MessageId"
    bool required = false;  // the class crashes or does nothing without it (proved by the class sweep)
};

/// A named starting point for placing a class: the args it is placed with (the Browser shows
/// each as its own card; e.g. a terrain brush's Box / Sphere / Cylinder / Ramp).
struct Preset {
    std::string label;
    std::vector< std::pair< int, std::int32_t > > args;  // Obj_arg index -> value
    std::array< float, 3 > scale{1.0f, 1.0f, 1.0f};      // the row's scale_x/y/z (a brush: 1 = 1000 units)
};

struct ClassInfo {
    std::string name;         // the placement name, "Kuribo"
    std::string displayName;  // "Goomba"
    std::string description;  // one line for the Browser card and the help strip
    std::vector< std::string > aliases;  // search terms: "goomba", "kuribo"
    Category category = Category::Other;
    bool placeable = true;    // false: internal or broken classes the Browser hides
    bool curated = false;     // names and help were written by hand (else generated)
    std::vector< Arg > args;
    std::vector< Switch > switches;
    std::vector< Link > links;
    std::string liveRecreate;  // Own-row sweep: safe, suspect, unsafe; empty = not classified.
    bool keepable = false;     // the placed transform is the actor's (UX "Keep"); false for rail riders, children
    bool hasCollision = false;
    bool bakesScale = false;  // generated geometry rebuilds collision at the row's full XYZ scale
    std::vector< std::string > archives;  // archives it loads (ObjectData/<name>.arc)
    std::string thumbnailArchive;  // the model to render for its thumbnail
    std::string thumbnailJoint;    // optional joint to frame
    std::string appearSound;       // SE name for placement feedback; empty: none
    std::vector< Preset > presets;  // empty: the class is placed with its args' defaults
    std::string table;  // the table it is placed in on the disc, layer left out ("jmp/mapparts/mappartsinfo"); empty: not on the disc
};

enum class PlacementLink : std::uint8_t { None, Optional, Required };
/// Donor wiring: common placement links plus class-specific catalog declarations. Required
/// links stay when placing a standalone copy; identity and ordinary settings aren't links.
PlacementLink placementLink(const ClassInfo* info, std::uint32_t field, const char* fieldName = nullptr);

/// The table a new row of `info` goes in, in `layer` ("common", "layera"): `table` with the layer
/// put in; when the disc never places the class, by category (areas, cameras) or objinfo.
std::string tablePath(const ClassInfo& info, const std::string& layer);

/// Loads the shipped catalog (generated.json, then curated.json, embedded in the binary). Called once;
/// later calls do nothing. False and *pError when the shipped files are missing or broken.
bool load(std::string* pError);
/// As load(), then the user's own files in <userDirectory>/editor/catalog/*.json (same schema as
/// native/editor/catalog/curated.json; they only describe classes the game has).
bool loadWithUser(const std::string& userDirectory, std::string* pError);

/// Adds planet classes (PlanetMapDataTable's PlanetName rows, read from the player's disc): the
/// factory makes any listed name a planet, so they are classes the game knows. Existing names are kept.
void addPlanets(const std::vector< std::string >& names);

/// Adds (or replaces) a class the factory doesn't make, defined by editor code (the terrain brush).
void addClass(ClassInfo info);

/// Every class, sorted by display name.
const std::vector< ClassInfo >& classes();
/// A class by its placement name (case-insensitive); null when unknown.
const ClassInfo* find(const char* pName);
/// Classes whose display name, name or alias contains `query` (case-insensitive), best first.
std::vector< const ClassInfo* > search(const std::string& query, int limit = 50);

}  // namespace PetariEditor::Catalog
