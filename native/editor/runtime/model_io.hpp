#pragma once
// Loading a galaxy for editing and saving its edits as a mod, without the game (TECH.md 5.1): the
// in-game Runtime and headless tools (petari-level) share these, so both load and save the same
// way. Needs the disc mounted (PetariNative::Platform::DVD::mount, with the mod overlay for
// readMounted). Not thread-safe; call from one thread.
#include "edit_model.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace PetariEditor::ModelIO {

/// A disc file ("/StageData/X.arc") as the game reads it: enabled mods' files win.
std::optional< Formats::Bytes > readMounted(const std::string& path);
/// The same file from the disc itself (no mods): the base of patches and of "Changed".
std::optional< Formats::Bytes > readUnmodded(const std::string& path);

/// The catalog (shipped + <userDirectory>/editor/catalog), the terrain brush class and checks,
/// classes other modules define (`registerClasses`, may be empty), the disc's planets, then the model of
/// `galaxy` mission `scenario`. Null and *pError when the galaxy can't be read; a catalog problem
/// is only reported (*pCatalogError) and the model still loads.
std::unique_ptr< EditModel > load(const std::string& galaxy, int scenario, const std::string& userDirectory,
                                  const std::function< void() >& registerClasses, std::string* pError, std::string* pCatalogError,
                                  const FileSource& mounted = readMounted, const FileSource& unmodded = readUnmodded);

/// What the editor reads for a galaxy: `mounted`, except archives the mod loader built from patch
/// mods (`builtFromPatch`), which are built again from the patches' row edits alone. The loader's
/// copies also have the asset builders' output in them (terrain bakes brush rows into planets),
/// which the editor must not see: it edits the rows the patches hold. `patches`: the enabled patch
/// mods' edits files, lowest priority first.
FileSource editingSource(const FileSource& mounted, const FileSource& unmodded, const std::vector< std::pair< std::string, std::string > >& patches,
                         const std::function< bool(const std::string& path) >& builtFromPatch);

/// Letters, digits, spaces, '-', '_' and '.'; 1-64 characters.
bool validModName(const std::string& name);

/// Saves the model's edits into `root` (a mod folder, mods/<Name>): a patch (edits/<Galaxy>.json)
/// unless `fullArchives` (files/StageData/*.arc, terrain brushes baked into planets); either form
/// removes the other's files for this galaxy; mod.txt (editor=1) when the folder is new. Refuses a
/// folder the editor didn't make. *pSummary: "3 edits as a patch", "2 archives and 1 terrain brush".
bool saveToDirectory(const EditModel& model, const std::filesystem::path& root, bool fullArchives, std::string* pSummary,
                     std::string* pError, const FileSource& mounted = readMounted);

}  // namespace PetariEditor::ModelIO
