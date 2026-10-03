#pragma once
// Loading a galaxy's EditModel from the mounted disc (petari/platform/dvd.hpp), as the game sees it.
// Game-free (the DVD layer and the mod folder only), so the in-game session (session.cpp) and headless
// tools (petari-level-x) load identically.
#include "edit_model.hpp"

#include <memory>
#include <optional>
#include <string>

namespace PetariEditor::Runtime {

/// A disc file through the mounted disc ("/StageData/X.arc"): enabled mod-folder files win, as for the game.
std::optional< Formats::Bytes > readMountedDisc(const std::string& path);
/// The disc itself, without the mod overlay: the EditModel's originals.
std::optional< Formats::Bytes > readUnmoddedDisc(const std::string& path);

/// The catalog (shipped + the user's files in ModFolder::userDirectory()) and the disc's planet classes
/// (PlanetMapDataTable's names). *pCatalogError: a catalog problem (the catalog is then partial).
void loadCatalog(std::string* pCatalogError);

/// The catalog (loadCatalog), then `stage`'s EditModel from the mounted disc, with the unmodded disc as its
/// originals. nullptr and *pError on failure.
std::unique_ptr< EditModel > loadEditModel(const char* pStage, int scenario, std::string* pError, std::string* pCatalogError);

}  // namespace PetariEditor::Runtime
