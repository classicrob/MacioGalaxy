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
// Pure C++: the editor's Full archives save (native/editor/runtime/save_mod.cpp) calls it with the
// stage archives that hold the brush rows; a patch mod's brushes are baked by the mod loader
// (bakePatchAssets, a Patch asset builder) from the archives its row edits built.
#include <petari/editor/formats/formats.hpp>
#include <petari/editor/patch.hpp>
#include <filesystem>
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

/// The patch-mod loader's asset builder (Patch::AssetBuilder): bakes the brush rows of the built
/// StageData archives in `pInOut`, replacing them and adding the planets and PlanetMapDataTable
/// (an earlier call's table is extended). Brush rows already baked are planet rows, so calling it
/// once per mod over the merged archives is idempotent.
bool bakePatchAssets(const std::filesystem::path& modRoot, const Patch::DiscReader& readDisc, std::vector< Patch::Built >* pInOut,
                     std::vector< std::string >* pLog, std::string* pError);
/// Registers bakePatchAssets with Patch::registerAssetBuilder (once; at start-up, before the mods
/// are resolved).
void registerPatchBuilder();

}  // namespace PetariEditor::Terrain
