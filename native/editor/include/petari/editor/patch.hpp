#pragma once
// Patch mods (docs/dev/level-editor/TECH.md 5.1, 5.3): a level edit saved as row operations
// against the player's own disc, `<mod>/edits/<Galaxy>.json`, with no Nintendo data in it. The
// mod loader turns enabled patches into archives at start-up (buildForLoader), cached in
// `<user>/mods-cache`. Pure C++ over the formats library: the editor, the loader and tests use it.
//
// The format (UTF-8 JSON):
//   { "format": 1, "galaxy": "EggStarGalaxy",
//     "archives": { "StageData/SoramameZone.arc": [ op, ... ] } }
// ops, applied per archive and per mod in this order (whatever order they are listed in):
//   { "op": "addFile",   "table": "jmp/placement/layerc/objinfo", "schemaOf": "jmp/placement/layera/objinfo" }
//   { "op": "addFile",   "table": ..., "columns": [ {"name": "pos_x", "type": "float"}, ... ] }
//   { "op": "addColumn", "table": ..., "name": "CommonPath_ID", "type": "long", "default": -1 }
//   { "op": "set",       "table": ..., "row": 12, "fp": "<16 hex>", "cells": { "pos_y": -12427.7, "Obj_arg0": 3 } }
//   { "op": "delete",    "table": ..., "row": 3,  "fp": "<16 hex>" }
//   { "op": "add",       "table": ..., "at": 17, "cells": { "name": "Kuribo", ... } }
// "row" is the row's index in the disc's table and "fp" its fingerprint (a different disc, or a
// region whose rows differ, refuses the archive). "at" is the row's index in the finished table.
// Column names are the game's field names, or "#xxxxxxxx" for a hash with no known name. Types:
// long, string, float, long2, short, byte, stringOffset.
#include <petari/editor/formats/formats.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace PetariEditor::Patch {

/// A row's fingerprint in a disc table (16 hex digits): its cells, strings included.
std::string fingerprint(const Formats::Table& table, int row);
/// "pos_x" -> its hash; "#00ab12cd" -> that hash.
std::uint32_t columnHash(const std::string& name);
const char* typeName(Formats::ColumnType type);
std::optional< Formats::ColumnType > typeFromName(const std::string& name);

/// One mod's edits file.
struct Source {
    std::string mod;   // the mod's folder name (for log lines)
    std::string text;  // edits/<Galaxy>.json
};

/// An archive built from the disc's and the patches that touch it.
struct Built {
    std::string path;               // "StageData/SoramameZone.arc"
    Formats::Bytes bytes;           // plain RARC
    std::vector< std::string > mods;  // the mods whose ops it carries, lowest priority first
};

/// Reads a disc file ("StageData/X.arc", as on the disc: Yaz0 or plain). nullopt: no such file.
using DiscReader = std::function< std::optional< Formats::Bytes >(const std::string& path) >;

/// Applies `patches` (lowest priority first: a later mod's set wins on the same cell) to the disc
/// archives they name. An archive a mod's ops don't fit (an unknown table or column, a
/// fingerprint that doesn't match) is built without that mod's ops, with a log line.
std::vector< Built > build(const std::vector< Source >& patches, const DiscReader& readDisc, std::vector< std::string >* pLog);

/// Makes a patch mod's generated files (terrain: brush models, PlanetMapDataTable rows) from the
/// archives its row edits built. `inOut` holds every built archive (plain RARC); the builder may
/// replace entries (same path) and append new ones ("ObjectData/EditorBrush1a2b.arc"). False and
/// *pError: the mod is broken, and the loader builds everything again without it.
using AssetBuilder = std::function< bool(const std::filesystem::path& modRoot, const DiscReader& readDisc, std::vector< Built >* inOut,
                                         std::vector< std::string >* pLog, std::string* pError) >;
/// Registers a builder for buildForLoader (at start-up, before the mods are resolved). `version`
/// is part of the cache key: change it when the builder's output changes.
void registerAssetBuilder(const std::string& version, AssetBuilder builder);

/// The mod loader's step (ModFolder::setPatchBuilder): builds the archives of the enabled patch
/// mods (`roots`, lowest priority first; each with edits/*.json) from `discFiles` (the disc's
/// files/ folder) into `cacheDirectory`, then runs the asset builders, unless an identical build is
/// there already. Returns (disc path, built file) pairs.
std::vector< std::pair< std::string, std::filesystem::path > > buildForLoader(const std::vector< std::filesystem::path >& roots,
                                                                             const std::filesystem::path& discFiles,
                                                                             const std::filesystem::path& cacheDirectory,
                                                                             std::vector< std::string >* pLog);

}  // namespace PetariEditor::Patch
