#pragma once
// Baking terrain brushes into the game's own formats (docs/dev/level-editor/TECH.md 6.3, "Bake"),
// so a saved mod plays without the editor, on Petari or anywhere the archives load:
//   - each EditorBrush row becomes a generated planet: ObjectData/<Planet>.arc holding
//     <planet>.bmd (its model, the material cloned from the player's disc), <planet>.kcl and
//     <planet>.pa; the row is renamed to <Planet> with scale 1 and its brush args cleared;
//   - each fitted gravity becomes a row beside it in the same layer's PlanetObjInfo
//     (GlobalCubeGravity, GlobalPointGravity or GlobalSegmentGravity), created from the zone's
//     Common PlanetObjInfo schema when the layer has none;
//   - ObjectData/PlanetMapDataTable.arc gains a row per planet.
// Planet names are "EditorBrush" + 8 hex digits of a hash of the brush's archive, table and
// values, so they are stable across saves and unique per brush.
//
// Pure C++: the editor's save (native/editor/runtime/save_mod.cpp) and the patch-mod builder both
// call it with the stage archives that hold the brush rows.
#include <petari/editor/formats/formats.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace PetariEditor::Terrain {

struct BakeInput {
    /// Stage archives that may hold brush rows: disc path ("/StageData/EggStarGalaxy.arc") ->
    /// the plain (or Yaz0) RARC.
    std::map< std::string, Formats::Bytes > stageArchives;
    /// Reads a disc file ("/ObjectData/FlowerPlanet.arc") for donor materials and
    /// PlanetMapDataTable; nullopt when missing.
    std::function< std::optional< Formats::Bytes >(const std::string&) > readDisc;
};

struct BakeOutput {
    /// Files to write (disc path -> plain RARC): the rewritten stage archives (only those that
    /// held brushes), the generated planet archives and PlanetMapDataTable.arc. Empty when
    /// there were no brushes.
    std::map< std::string, Formats::Bytes > files;
    int brushes = 0;
    std::vector< std::string > log;  // one line per brush
    std::string error;               // empty on success; nothing is usable when set
};

BakeOutput bakeBrushes(const BakeInput& input);

}  // namespace PetariEditor::Terrain
