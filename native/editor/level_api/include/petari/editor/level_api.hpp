#pragma once
// The agent level-authoring API (docs/dev/level-editor/AGENT_API.md): the in-game editor's own core
// (EditModel, ops and undo, the catalog, Checks, patch-mod save) driven by JSON commands, without the
// game or a renderer. petari-level (the CLI), its stdio server and the test server's live mode all
// go through Level::call, so they share one command set, one op format and one set of error codes.
//
// A Level is one galaxy (one mission selected) loaded from the player's disc, plus a journal: the
// batches applied since it was opened, each one EditModel op (one undo step). The journal is plain
// JSON (low-level ops with stable row refs), so a workspace replays it after a restart and live mode
// sends the same ops to a running game.
#include <petari/editor/edit_api.hpp>
#include <petari/editor/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PetariEditor {
class EditModel;
}

namespace PetariEditor::LevelApi {

/// A failure an agent can act on. `code` is stable (see errorCodes()); `hint` says what to do next;
/// `candidates` are the closest valid values (class names, refs, arg names, labels).
struct Error {
    std::string code;     // "unknown-class", "bad-ref", "ambiguous-ref", "bad-arg", "bad-value", "op-failed", ...
    std::string message;  // one sentence
    std::string hint;     // optional: what to try instead
    std::vector< std::string > candidates;
    std::string where;    // optional: "ops[2].args.Coins inside"
    Json::Value toJson() const;
};

struct OpenOptions {
    std::filesystem::path disc;  // the extracted disc (has files/)
    std::filesystem::path user;  // a Petari user directory: the catalog's user files, where save writes mods; empty: none
    bool userMods = false;       // load on top of the user directory's enabled mods (as the game would)
    std::string galaxy;          // "EggStarGalaxy"
    int mission = 1;
};

/// One command's description for help/schemas (every command is self-describing).
struct CommandSpec {
    std::string name;      // "list"
    std::string summary;   // one line
    std::string argsDoc;   // the args, one per line: "class  string  class name or display name (optional)"
    Json::Value schema;    // JSON Schema of the args object
    std::string example;   // a JSON args example
    bool mutates = false;  // changes the journal (the CLI saves the workspace after it)
};

class Level {
public:
    ~Level();
    /// Mounts the disc (with the user's enabled mods when asked) and loads the galaxy. nullptr and *pError.
    static std::unique_ptr< Level > open(const OpenOptions& options, Error* pError);

    /// Runs one command: {"ok": true, ...result} or {"ok": false, "error": {...}}. Never throws.
    Json::Value call(const std::string& command, const Json::Value& args);
    /// `help` without a loaded galaxy (the CLI's help needs no disc).
    static Json::Value help(const Json::Value& args);

    /// The journal (to persist a workspace) and its replay (after open, before anything else).
    Json::Value journal() const;
    bool replay(const Json::Value& journal, Error* pError);

    const EditModel& model() const;
    const OpenOptions& options() const { return mOptions; }

    /// Live mode: called with each committed op (apply, undo as an inverse is not sent: see AGENT_API.md).
    void setOnCommit(std::function< void(const Json::Value& opJson) > fn) { mOnCommit = std::move(fn); }

    struct Impl;  // level_internal.hpp

private:
    Level();
    OpenOptions mOptions;
    std::unique_ptr< Impl > mImpl;
    std::function< void(const Json::Value&) > mOnCommit;
};

/// Every command, in help order.
const std::vector< CommandSpec >& commands();
/// The batch op kinds ("add", "move", ...) with their docs and schemas.
const std::vector< CommandSpec >& opKinds();
/// Error codes and what each means.
const std::vector< std::pair< std::string, std::string > >& errorCodes();

// ---- low-level ops as JSON (the journal and live mode) ----------------------------------------------
// {"label": "...", "adds": [{"id": "new:1", "zone": "EggStarGalaxy", "table": "jmp/placement/common/objinfo",
//   "cells": {"name": "Coin", "pos_x": 0, ...}}], "sets": [{"row": "EggStarGalaxy:common/objinfo:12",
//   "cells": {"pos_y": 500}}], "deletes": ["..."], "reverts": ["..."]}
// Row refs: original rows "<Zone>:<table>:<source index>" (the table path without "jmp/placement/" or
// "jmp/"), added rows "new:<n>". Both survive reloads, so a running game resolves them too.

/// A row's ref, or "" for an unknown row.
std::string rowRef(const ModelView& model, RowId row);
/// A ref back to a row id (added rows: their id from the ref's n). Invalid when it names no row.
RowId parseRowRef(const ModelView& model, const std::string& ref);
/// The id the API gives the n-th added row (bit 63 | bit 62 | n: apart from newRowId()'s ids).
RowId newRowIdFor(std::uint64_t n);

Json::Value opToJson(const ModelView& model, const Op& op);
/// nullopt and *pError when a ref, zone or table can't be resolved.
bool opFromJson(const ModelView& model, const Json::Value& json, Op* pOp, std::string* pError);

}  // namespace PetariEditor::LevelApi
