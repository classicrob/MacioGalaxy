#pragma once
// Shared between the level API's source files (not installed).
#include <petari/editor/catalog.hpp>
#include <petari/editor/gizmo_math.hpp>
#include <petari/editor/level_api.hpp>

#include "edit_model.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace PetariEditor::LevelApi {

namespace G = PetariEditor::Gizmo;

/// Thrown inside the API and turned into {"ok": false, "error": ...} by Level::call.
struct Failure {
    Error error;
};
[[noreturn]] void fail(const std::string& code, const std::string& message, const std::string& hint = "",
                       std::vector< std::string > candidates = {}, const std::string& where = "");

std::string shortTablePath(const std::string& path);
std::string lower(std::string s);
std::string columnLabel(std::uint32_t hash);  // known name or "#xxxxxxxx"
std::uint32_t columnHashOfName(const std::string& name);  // "pos_x" or "#xxxxxxxx"
/// A JSON value as a cell of column `name` (its type from `columns`, else from {"type", "value"}, the name or the value).
bool parseCell(const Json::Value& value, const std::string& name, const std::vector< Column >* pColumns, Cell* pCell, std::string* pError);
/// An arg's value as the API shows it: null = the class default, enum labels, bools.
Json::Value argValueJson(const Catalog::Arg& arg, std::int32_t value);
Json::Value helpJson(const Json::Value& args);

// ---- geometry ------------------------------------------------------------------------------------
struct Frame {
    G::V3 origin;
    G::Basis basis{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};
Frame zoneFrame(const ModelView& model, int zone);
G::V3 toWorld(const Frame& f, G::V3 p);
G::V3 toLocal(const Frame& f, G::V3 p);
G::Basis basisToWorld(const Frame& f, const G::Basis& b);
G::Basis basisToLocal(const Frame& f, const G::Basis& b);
G::V3 v3(const Vec3& v);
Json::Value vecJson(G::V3 v, int decimals = 2);
/// [x, y, z] (or {"x":..}) -> V3; fails with `where` on a bad value.
G::V3 parseVec(const Json::Value& value, const std::string& where);

// ---- the level's state -------------------------------------------------------------------------------
struct JournalEntry {
    std::string key;    // the batch's idempotency key ("" = none)
    std::string label;
    Json::Value op;     // opToJson of the merged op
};

class World;

struct Level::Impl {
    Impl();
    ~Impl();
    std::unique_ptr< EditModel > model;
    std::unique_ptr< World > worldCache;
    /// The collision world, synced to the model (world.hpp).
    World& world();
    std::set< std::string > baseline;           // check keys of the galaxy as loaded (not reported)
    std::vector< JournalEntry > entries;        // parallel to the model's history
    std::map< std::string, RowId > labels;      // "star1" -> row
    std::uint64_t nextNew = 1;                  // the next "new:<n>"

    // queries (query.cpp)
    RowId resolveRef(const Json::Value& ref, const std::string& where) const;
    std::vector< RowId > resolveTargets(const Json::Value& op, const std::string& where) const;  // ref | refs | select
    std::vector< RowId > select(const Json::Value& filter, const std::string& where) const;
    const Catalog::ClassInfo* resolveClass(const std::string& name, bool required, const std::string& where) const;
    Json::Value objectJson(RowId row, bool full) const;
    std::string labelOf(RowId row) const;
    std::vector< RowId > allRows() const;  // live rows, galaxy order

    // ops (ops.cpp)
    Json::Value applyBatch(const Json::Value& args, bool dryRun);
    Json::Value checksJson(bool all) const;
    std::vector< Check > newChecks() const;
};

}  // namespace PetariEditor::LevelApi
