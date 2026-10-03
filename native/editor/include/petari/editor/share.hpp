#pragma once
// Sharing mods (docs/dev/level-editor/UX.md 17, "Share" and "Installing"): a mod folder as a .zip
// to hand to someone, and installing one. Pure C++ (zlib for deflate); the UI's sheets call these
// and the app's drop handler calls inspect/install. Files only: no game state.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace PetariEditor::Share {

/// What the Share sheet adds to the mod's mod.txt (blank fields keep what is there).
struct Info {
    std::string title;        // name=
    std::string author;       // author=
    std::string version;      // version=
    std::string description;  // description= (one line; newlines become spaces)
    std::vector< std::string > needs;   // needs= other mods it relies on (folder names), comma-separated
    std::vector< std::uint8_t > previewPng;  // preview.png; empty: keep the mod's own, if any
};

/// What a mod (a folder or a zip) is, for the Install sheet.
struct Preview {
    std::string folder;       // the mod's folder name (the zip's top folder)
    std::string title, author, version, description;
    std::vector< std::string > needs;
    std::vector< std::uint8_t > previewPng;
    std::vector< std::string > galaxies;  // galaxies its level edits change (edits/<Galaxy>.json) and new ones (galaxy.json)
    std::vector< std::string > archives;  // "StageData/SoramameZone.arc": archives the edits touch, then files/ archives
    bool editorMod = false;   // made by the level editor (mod.txt editor=1): "Open in editor" applies
    bool fullArchives = false;  // files/ replaces disc files: it carries game data (warn before sharing)
    int fileCount = 0;
};

/// Writes `modRoot` (mods/<Name>) to `zipPath` as <Name>/... after putting `info` into its mod.txt
/// and preview.png. Leaves out host clutter (.DS_Store) and the editor's caches.
bool exportZip(const std::filesystem::path& modRoot, const Info& info, const std::filesystem::path& zipPath, std::string* pError);

/// Reads a mod folder's description.
Preview describeFolder(const std::filesystem::path& modRoot);
/// Reads a zip made by exportZip (or any zip with one top folder holding mod.txt, edits/ or files/).
std::optional< Preview > inspectZip(const std::filesystem::path& zipPath, std::string* pError);

/// Unpacks a mod zip into modsDirectory/<folder>. Refuses paths that escape the folder, an
/// existing folder (unless `replace`; never one the editor didn't make from a zip of the same
/// name), and more than 2 GB. *pFolder: the installed folder's name.
bool installZip(const std::filesystem::path& zipPath, const std::filesystem::path& modsDirectory, bool replace, std::string* pFolder,
                std::string* pError);

// ---- zip (exposed for tests) ----
struct ZipEntry {
    std::string path;  // "Name/edits/EggStarGalaxy.json"
    std::vector< std::uint8_t > data;
};
std::vector< std::uint8_t > writeZip(const std::vector< ZipEntry >& entries);
std::optional< std::vector< ZipEntry > > readZip(const std::vector< std::uint8_t >& zip, std::string* pError, std::uint64_t limit = 2ull << 30);

}  // namespace PetariEditor::Share
