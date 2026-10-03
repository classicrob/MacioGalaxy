// The self-description: every command and batch op with its args, a JSON Schema and an example
// (`help`, `help --command X`, `help --op add`), and the error codes.
#include "level_internal.hpp"

namespace PetariEditor::LevelApi {

namespace {
    const char* const kRefDoc =
        "Refs name one row: \"<Zone>:<table>:<index>\" (an original row: its table without jmp/placement/, its index in the loaded base (the disc when no mods), "
        "e.g. \"EggStarGalaxy:common/objinfo:12\"), \"new:<n>\" (a row this workspace added), \"@label\" (set with \"as\"), "
        "\"<Class>#<n>\" (the n-th row of a class, galaxy order) or a selector object {\"class\", \"zone\", \"near\": [x,y,z], "
        "\"radius\", \"mission\", \"category\", \"table\", \"text\", \"nth\"}. Refs are stable across undo, reload and restarts.";

    const char* const kEditDoc =
        "pos      [x,y,z]   world position\n"
        "zone_pos [x,y,z]   position in the row's zone\n"
        "by       [x,y,z]   move by (world)\n"
        "rot      [x,y,z]   world rotation, degrees (the game's Rz*Ry*Rx)\n"
        "zone_rot [x,y,z]   rotation in the zone (the row's dir_x/y/z)\n"
        "up       [x,y,z]   stand the object up along this direction (keeps its heading)\n"
        "front    [x,y,z]   face this way (with up)\n"
        "turn     number    degrees about its own up\n"
        "scale    n|[x,y,z]\n"
        "args     {name: value}  by the catalog's arg names (`class X`), or Obj_arg0..7; enum labels; true/false; null = the class default\n"
        "switches {SW_APPEAR: n|null|\"new\"|\"@ref.SW_DEAD\"}  \"new\" picks a free zone switch (0-127); \"<ref>.<field>\" copies another "
        "row's\n"
        "links    {CommonPath_ID: n|null, GroupId: n, ...}\n"
        "fields   {raw field: value}  any column by its name\n"
        "on_ground bool     drop onto the ground below the point along gravity (headless collision; see `ground`)\n"
        "height   number    with on_ground: this far above the ground, along gravity\n"
        "down     [x,y,z]   the gravity to use instead of the approximate one\n"
        "align    bool      stand up against gravity (default: on with on_ground)";

    std::vector< CommandSpec > makeCommands() {
        auto S = [](const char* text) {
            std::string error;
            std::optional< Json::Value > v = Json::parse(text, &error);
            return v ? *v : Json::Value::object();
        };
        std::vector<CommandSpec> result = {
            {"help", "This overview, or one command / op in full",
             "command  string  a command name (optional)\nop       string  a batch op name (optional)",
             S(R"({"type":"object","properties":{"command":{"type":"string"},"op":{"type":"string"}}})"), R"({"op": "add"})", false},
            {"info", "The galaxy: missions, zones (placed, origin), counts, history, unsaved, new problems, labels", "",
             S(R"({"type":"object"})"), "{}", false},
            {"refreeze", "Explicitly replace the frozen mod base with the current inputs; clear the journal",
             "discard_journal bool  required when history exists; save first",
             S(R"({"type":"object","properties":{"discard_journal":{"type":"boolean"}},"additionalProperties":false})"),
             R"({"discard_journal":true})", true},
            {"galaxies", "Galaxy names on the disc (StageData/<G>/<G>Scenario.arc)", "", S(R"({"type":"object"})"), "{}", false},
            {"catalog", "Search the object catalog by words (display names, class names, aliases)",
             "query        string  words, e.g. \"launch star\" (optional: every class)\ncategory     string  Enemies, Items, Platforms, Planets, "
             "Gravity, Areas, "
             "Cameras, NPCs, Launchers, Decoration, Logic, Other\nplaced_here  bool    only classes this galaxy places\nlimit        int     "
             "default 30\nall          bool    include "
             "unplaceable classes",
             S(R"({"type":"object","properties":{"query":{"type":"string"},"category":{"type":"string"},"placed_here":{"type":"boolean"},"limit":{"type":"integer"},"all":{"type":"boolean"}}})"),
             R"({"query": "coin"})", false},
            {"class",
             "One class in full: args (names, types, enum choices, defaults), switches, links (required?), presets, table, examples here",
             "class  string  class name, display name or alias (\"Kuribo\", \"Goomba\")",
             S(R"({"type":"object","required":["class"],"properties":{"class":{"type":"string"}}})"), R"({"class": "Power Star"})", false},
            {"list", "Objects matching a filter (summaries; full for args/switches/links/fields)",
             "class, zone, category, mission, table, state (original|changed|added), text, near [x,y,z], radius: filters (all optional)\nfull    "
             "bool  every field\nlimit   int   "
             "default 200\noffset  int",
             S(R"({"type":"object","properties":{"class":{"type":"string"},"zone":{"type":["string","integer"]},"category":{"type":"string"},"mission":{"type":"integer"},"table":{"type":"string"},"state":{"type":"string"},"text":{"type":"string"},"near":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"radius":{"type":"number"},"full":{"type":"boolean"},"limit":{"type":"integer"},"offset":{"type":"integer"}}})"),
             R"({"class": "Coin", "near": [0, 0, 0], "radius": 3000})", false},
            {"get", "One object (or several) in full", "ref   ref    (see refs)\nrefs  [ref]",
             S(R"({"type":"object","properties":{"ref":{},"refs":{"type":"array"}}})"), R"({"ref": "PowerStar#0"})", false},
            {"apply", "Apply a batch of ops as ONE transaction and ONE undo step (all or nothing)",
             "ops      [op]    see `help --op <name>`\nkey      string  idempotency key: a batch with an applied key is skipped\nlabel    string "
             " the undo label\ndry_run  bool    "
             "validate and report (created, changed, checks) without committing\nshow_op  bool    include the merged low-level op",
             S(R"({"type":"object","required":["ops"],"properties":{"ops":{"type":"array","items":{"type":"object","required":["op"]}},"key":{"type":"string"},"label":{"type":"string"},"dry_run":{"type":"boolean"},"show_op":{"type":"boolean"}}})"),
             R"({"key": "coins-1", "ops": [{"op": "add", "class": "Coin", "pos": [0, 1300, 0], "as": "c1"}]})", true},
            {"undo", "Undo the last batch (or N)", "steps  int  default 1", S(R"({"type":"object","properties":{"steps":{"type":"integer"}}})"),
             "{}", true},
            {"redo", "Redo an undone batch (or N)", "steps  int  default 1", S(R"({"type":"object","properties":{"steps":{"type":"integer"}}})"),
             "{}", true},
            {"history", "The journal: each batch's label and key, applied or undone", "", S(R"({"type":"object"})"), "{}", false},
            {"checks", "Problems the edits introduced (the in-game Checks; the shipped galaxy's own are left out)",
             "all  bool  include the shipped galaxy's problems (marked shipped)",
             S(R"({"type":"object","properties":{"all":{"type":"boolean"}}})"), "{}", false},
            {"diff", "Rows added or changed against the disc, with [disc, now] cells", "", S(R"({"type":"object"})"), "{}", false},
            {"save", "Save as a mod in <user>/mods/<mod> (a patch by default: no game data), turned on for the next start",
             "mod   string  the mod folder name (letters, digits, space, - _ .)\nfull  bool    Full archives instead of a patch",
             S(R"({"type":"object","required":["mod"],"properties":{"mod":{"type":"string"},"full":{"type":"boolean"}}})"),
             R"({"mod": "Coin Arc"})", false},
            {"ground", "The ground below a point along gravity (headless: the placed rows' collision; gravity approximate)",
             "pos   [x,y,z]  where to look from\ndown  [x,y,z]  gravity to use (default: from the gravity rows, else the nearest surface)\nmax   "
             "number   how far (default 50000)",
             S(R"({"type":"object","required":["pos"],"properties":{"pos":{"type":"array"},"down":{"type":"array"},"max":{"type":"number"}}})"),
             R"({"pos": [0, 3000, 0]})", false},
            {"gravity", "The gravity direction at a point and the field it comes from (approximate headless; exact in live mode)", "pos  [x,y,z]",
             S(R"({"type":"object","required":["pos"],"properties":{"pos":{"type":"array"}}})"), R"({"pos": [0, 3000, 0]})", false},
            {"colliders", "The collision the headless placement helpers use: rows, triangle counts, bounds", "", S(R"({"type":"object"})"), "{}",
             false},
            {"export", "The journal's low-level ops (for live mode or another workspace)",
             "from  int  first entry (default 0)\nto    int  past the last (default: the cursor)",
             S(R"({"type":"object","properties":{"from":{"type":"integer"},"to":{"type":"integer"}}})"), "{}", false},
        };
        for (auto& spec : result) {
            Json::Value properties = spec.schema["properties"].isObject() ? spec.schema["properties"] : Json::Value::object();
            properties.set("existing_checks", S(R"({"type":"boolean"})"));
            spec.schema.set("properties", std::move(properties));
            spec.argsDoc += "\nexisting_checks  bool  include currently present baseline checks (default false)";
        }
        return result;
    }

    std::vector< CommandSpec > makeOps() {
        auto S = [](const char* text) {
            std::optional< Json::Value > v = Json::parse(text);
            return v ? *v : Json::Value::object();
        };
        const std::string edits = kEditDoc;
        return {
            {"add", "Place a new object",
             "class    string  class, display name or alias (required unless like)\nzone     string  default: the galaxy's own zone (or "
             "like's)\nlayer    string  "
             "common (every mission, default) or a-p\nmissions [int]   pick the layer active in exactly these missions\nlike     ref     copy "
             "this row's settings (the "
             "Browser's place)\npreset   string  a catalog preset's args\npath     ref     link to this path (a Launch Star's flight); the star then faces along it\nface_path bool   with path: face along its first segment (default: yes unless rot/up/front/turn is given)\nas       "
             "string  a label for later refs (\"@as\")\nforce    bool    allow a class the catalog "
             "doesn't know\n" +
                 edits,
             S(R"({"type":"object","required":["op"],"properties":{"op":{"const":"add"},"class":{"type":"string"},"zone":{"type":["string","integer"]},"layer":{"type":"string"},"missions":{"type":"array","items":{"type":"integer"}},"like":{},"preset":{"type":"string"},"as":{"type":"string"},"force":{"type":"boolean"},"pos":{"type":"array"},"zone_pos":{"type":"array"},"rot":{"type":"array"},"up":{"type":"array"},"front":{"type":"array"},"turn":{"type":"number"},"scale":{},"args":{"type":"object"},"switches":{"type":"object"},"links":{"type":"object"},"fields":{"type":"object"}}})"),
             R"({"op": "add", "class": "PowerStar", "pos": [0, 1500, 0], "args": {"Star ID": 1}, "as": "star"})", true},
            {"array", "Many copies of one add along a shape: line, arc (a coin arc), ring, grid, path (a polyline); each may snap to the ground",
             "shape    string  line | arc | ring | grid | path\nline/arc: start [x,y,z], end [x,y,z] (or from/to), count | spacing; arc: peak "
             "(height at the middle, along -gravity)\n"
             "ring:    center, radius, count | spacing, start_angle (degrees), up/front (the plane)\ngrid:    origin (the centre), size [nx, "
             "nz], spacing n|[sx,sz], up/front\n"
             "path:    points [[x,y,z], ...], count | spacing, closed; or along: a rail ref (shape optional)\nalong:   arc-length sampling with "
             "256 samples per Bezier segment; spacing stays K units\nface_along bool  each copy faces the next\nas       string  labels as.0, "
             "as.1, ...\n"
             "and every add key (class, args, on_ground, height, ...)",
             S(R"({"type":"object","required":["op","class"],"properties":{"op":{"const":"array"},"shape":{"enum":["line","arc","ring","grid","path"]},"class":{"type":"string"},"start":{"type":"array"},"end":{"type":"array"},"from":{"type":"array"},"to":{"type":"array"},"count":{"type":"integer"},"spacing":{},"peak":{"type":"number"},"center":{"type":"array"},"radius":{"type":"number"},"start_angle":{"type":"number"},"origin":{"type":"array"},"size":{"type":"array"},"points":{"type":"array"},"along":{},"closed":{"type":"boolean"},"face_along":{"type":"boolean"},"as":{"type":"string"}}})"),
             R"({"op": "array", "class": "Coin", "shape": "arc", "start": [0, 1200, 0], "end": [1200, 1200, 0], "peak": 600, "count": 8, "as": "arc"})",
             true},
            {"ground_array", "Probe all placements first, then add a ground-snapped array in one transaction",
             "The array keys: class, shape | along, shape coordinates, count | spacing, height, down, align, front, face_along and add keys. All rays use the world before any copies are created. on_ground cannot be false.",
             S(R"({"type":"object","required":["op","class"],"properties":{"op":{"const":"ground_array"},"class":{"type":"string"},"shape":{"enum":["line","arc","ring","grid","path"]},"along":{},"height":{"type":"number"},"down":{"type":"array"},"front":{"type":"array"},"align":{"type":"boolean"}}})"),
             R"({"op":"ground_array","class":"Coin","shape":"line","start":[0,3000,0],"end":[1000,3000,0],"count":6,"height":100,"front":[1,0,0]})", true},
            {"path", "Make a path (rail) from world points, smooth by default; label it to link objects to it",
             "points    [[x,y,z] | {pos, in, out}]  2+ points, world space (in/out: explicit Bezier handles)\nzone      string  the zone it "
             "belongs to (objects link to rails of their own zone; default the galaxy's)\n"
             "closed    bool\nsmooth    bool    Catmull-Rom handles where none are given (default true)\non_ground bool    each point onto the "
             "ground (with height, down)\nas        string  a label",
             S(R"({"type":"object","required":["op","points"],"properties":{"op":{"const":"path"},"points":{"type":"array","minItems":2},"zone":{"type":["string","integer"]},"closed":{"type":"boolean"},"smooth":{"type":"boolean"},"on_ground":{"type":"boolean"},"height":{"type":"number"},"as":{"type":"string"}}})"),
             R"({"op": "path", "points": [[0, 1500, 0], [2000, 3000, 0], [4000, 1500, 0]], "as": "flight"})", true},
            {"gravity-shape", "Change a gravity field shape with the editor's canonical op (safe arg defaults)",
             "ref | refs | select  gravity fields\nshape  Point | Cube | Cone | Disk | Torus | Parallel | Segment | Wire\nbound  Sphere | Box | Cylinder (Parallel only)",
             S(R"({"type":"object","required":["op","shape"],"properties":{"op":{"const":"gravity-shape"},"ref":{},"refs":{"type":"array"},"select":{"type":"object"},"shape":{"enum":["Point","Cube","Cone","Disk","Torus","Parallel","Segment","Wire"]},"bound":{"enum":["Sphere","Box","Cylinder"]}}})"),
             R"({"op":"gravity-shape","ref":"@gravity","shape":"Parallel","bound":"Box"})", true},
            {"gravity-values", "Edit gravity values with the editor's canonical numeric op",
             "ref | refs | select  gravity fields\nvalues  object: Range, Distant, Priority, Inverse, Obj_arg0..7. Integers for Priority, Inverse and args; Inverse also takes bool. Range < 0 becomes -1; Distant/Priority clamp to 0.",
             S(R"({"type":"object","required":["op","values"],"properties":{"op":{"const":"gravity-values"},"ref":{},"refs":{"type":"array"},"select":{"type":"object"},"values":{"type":"object","minProperties":1}}})"),
             R"({"op":"gravity-values","ref":"@gravity","values":{"Range":3000,"Distant":0,"Priority":2,"Inverse":false}})", true},
            {"move-point", "Move a path point and both of its handles with the editor's canonical op",
             "ref | refs | select  path points (get the rail for their refs)\npos | zone_pos | by  exactly one; pos/by are world space",
             S(R"({"type":"object","required":["op"],"properties":{"op":{"const":"move-point"},"ref":{},"refs":{"type":"array"},"select":{"type":"object"},"pos":{"type":"array"},"zone_pos":{"type":"array"},"by":{"type":"array"}}})"),
             R"({"op":"move-point","ref":"@mid","by":[0,100,0]})", true},
            {"move-handle", "Move one absolute Bezier handle of a path point",
             "ref | refs | select  path points\nhandle  in | out\npos | zone_pos | by  exactly one; pos/by are world space",
             S(R"({"type":"object","required":["op","handle"],"properties":{"op":{"const":"move-handle"},"handle":{"enum":["in","out"]},"ref":{},"refs":{"type":"array"},"select":{"type":"object"},"pos":{"type":"array"},"zone_pos":{"type":"array"},"by":{"type":"array"}}})"),
             R"({"op":"move-handle","ref":"@mid","handle":"out","by":[100,0,0]})", true},
            {"insert-point", "Split a Bezier segment without changing its curve (one transaction)",
             "path  rail ref\nsegment  zero-based start point index\nt  number 0.01..0.99 (default .5)\nas  label for the created point", S(R"({"type":"object","required":["op","path","segment"],"additionalProperties":false,"properties":{"op":{"const":"insert-point"},"path":{},"segment":{"type":"integer","minimum":0},"t":{"type":"number","minimum":0.01,"maximum":0.99},"as":{"type":"string"}}})"),
             R"({"op":"insert-point","path":"@flight","segment":0,"t":0.5,"as":"mid"})", true},
            {"delete-point", "Delete path points and renumber the survivors; keep at least two", "ref | refs | select  path points",
             S(R"({"type":"object","required":["op"],"properties":{"op":{"const":"delete-point"},"ref":{},"refs":{"type":"array"},"select":{"type":"object"}}})"),
             R"({"op":"delete-point","ref":"@mid"})", true},
            {"link", "Link objects to a path (their CommonPath_ID; Paths::linkPath: same zone, 2+ points); add takes \"path\" too",
             "ref | refs | select  the objects\npath  ref  the path's row (\"@flight\")\nface_path  bool  also turn Launch/Sling Stars to face along it (default false here)", Json::Value::object(),
             R"({"op": "link", "ref": "@launch", "path": "@flight"})", true},
            {"set", "Change objects: transform, args, switches, links, raw fields (move/rotate/scale/transform are aliases)",
             "ref | refs | select  the rows (select may carry \"expect\": n)\n" + edits,
             S(R"({"type":"object","required":["op"],"properties":{"op":{"enum":["set","move","rotate","scale","transform"]},"ref":{},"refs":{"type":"array"},"select":{"type":"object"}}})"),
             R"({"op": "set", "ref": "@star", "args": {"Star ID": 2}})", true},
            {"move", "Alias of set", "", Json::Value::object(), R"({"op": "move", "ref": "Coin#0", "by": [0, 200, 0]})", true},
            {"rotate", "Alias of set", "", Json::Value::object(), R"({"op": "rotate", "ref": "@star", "up": [0, 1, 0], "turn": 90})", true},
            {"scale", "Alias of set", "", Json::Value::object(), R"({"op": "scale", "ref": "@brush", "scale": [2, 1, 2]})", true},
            {"delete", "Delete objects", "ref | refs | select",
             S(R"({"type":"object","required":["op"],"properties":{"op":{"const":"delete"},"ref":{},"refs":{"type":"array"},"select":{"type":"object"}}})"),
             R"({"op": "delete", "select": {"class": "Kuribo", "zone": "EggStarGalaxy", "expect": 3}})", true},
            {"duplicate", "Copy an object (every field) and change the copy", "ref  ref\nas   string\n" + edits,
             S(R"({"type":"object","required":["op","ref"],"properties":{"op":{"const":"duplicate"},"ref":{},"as":{"type":"string"}}})"),
             R"({"op": "duplicate", "ref": "@c1", "by": [300, 0, 0], "as": "c2"})", true},
            {"revert", "Put rows back to the disc's values", "ref | refs | select", Json::Value::object(),
             R"({"op": "revert", "ref": "Kuribo#0"})", true},
            {"fix", "Apply a check's automatic fix", "check  string  the check's key (`checks`)",
             S(R"({"type":"object","required":["op","check"],"properties":{"op":{"const":"fix"},"check":{"type":"string"}}})"),
             R"({"op": "fix", "check": "switch-range:SW_APPEAR:..."})", true},
            {"label", "Name an existing row for later refs", "ref  ref\nas   string", Json::Value::object(),
             R"({"op": "label", "ref": "PowerStar#0", "as": "star"})", true},
            {"raw", "A low-level op (the journal format: adds/sets/deletes/reverts by ref and raw cells)", "edit  object  see export",
             Json::Value::object(), R"({"op": "raw", "edit": {"sets": [{"row": "EggStarGalaxy:common/objinfo:3", "cells": {"pos_y": 900}}]}})",
             true},
        };
    }
}  // namespace

const std::vector< CommandSpec >& commands() {
    static const std::vector< CommandSpec > sCommands = makeCommands();
    return sCommands;
}

const std::vector< CommandSpec >& opKinds() {
    static const std::vector< CommandSpec > sOps = makeOps();
    return sOps;
}

const std::vector< std::pair< std::string, std::string > >& errorCodes() {
    static const std::vector< std::pair< std::string, std::string > > sCodes = {
        {"bad-args", "a missing, unknown or conflicting key (see where and help)"},
        {"bad-value", "a value of the wrong type or out of range"},
        {"bad-ref", "a ref or selector names no live row (deleted, undone, typo)"},
        {"ambiguous-ref", "a selector matched several rows where one is needed (add nth or near)"},
        {"unknown-class", "no catalog class by that name (candidates: the closest)"},
        {"bad-arg", "the class has no arg by that name (candidates: its args)"},
        {"bad-op", "no batch op by that name"},
        {"op-failed", "the editor core refused an op (message has its reason)"},
        {"no-fix", "a check has no automatic fix"},
        {"capacity", "a limit was reached (zone switches)"},
        {"no-ground", "on_ground found no collision below the point along gravity"},
        {"inside-collision", "on_ground started inside a collider (message says how far below its surface; raise the point)"},
        {"unknown-command", "no command by that name"},
        {"unknown-galaxy", "no galaxy by that name on the disc"},
        {"bad-disc", "no extracted disc at the path"},
        {"bad-mod", "an input patch cannot be resolved against this disc"},
        {"bad-workspace", "the frozen base is missing or no longer matches its identity"},
        {"stale-source", "an input changed; explicitly refreeze to adopt it"},
        {"base-mismatch", "the running game and workspace have different loaded bases"},
        {"io", "a workspace or input file could not be read or written"},
        {"load-failed", "the galaxy's archives could not be read"},
        {"journal", "the workspace's journal no longer applies to the disc"},
        {"no-user", "save needs a user directory"},
        {"save-failed", "the mod could not be written"},
        {"nothing-to-undo", "the journal is at its start"},
        {"nothing-to-redo", "the journal is at its end"},
        {"internal", "a bug: please report the command and args"},
        {"not-playable", "live playback needs controllable Mario in normal Play"},
        {"busy", "another replay or input plan owns the controls; cancel it first"},
        {"bad-file", "a live playback request file could not be read"},
        {"bad-json", "a live playback request is not valid JSON"},
        {"route", "the recorded waypoint route could not be started"},
        {"inputs", "the frame-held input engine refused the plan"},
        {"timeout", "live verification timed out; the wrapper cancelled controls"},
    };
    return sCodes;
}

const std::vector< CommandSpec >& liveCommands() {
    auto schema = [](const char* text) { return *Json::parse(text); };
    static const std::vector<CommandSpec> specs = {
        {"verify-route", "Replay a waypoint route; verdict requires Star.Get and playable observatory return",
         "file string world x,y,z,action CSV\nframes integer 60..1080000 (default 18000)\nrequire_star bool (default true); false verifies "
         "endpoint and landing",
         schema(
             R"({"type":"object","required":["file"],"additionalProperties":false,"properties":{"file":{"type":"string"},"frames":{"type":"integer","minimum":60,"maximum":1080000},"require_star":{"type":"boolean"}}})"),
         R"({"file":"build/course.csv","require_star":true})", false},
        {"verify-route-status", "Replay active/finished/passed/reason/frame", "", schema(R"({"type":"object"})"), "{}", false},
        {"verify-route-cancel", "Release controls and cancel the waypoint replay", "", schema(R"({"type":"object"})"), "{}", false},
        {"inputs-start", "Frame-exact held controls through the shared input engine",
         "array of {frames,buttons?,stick?}; controls jump/action/spin/crouch/cappy/camera_left/camera_right/plus/minus and "
         "forward/back/left/right\nstick [right,forward], axes -1..1; cannot combine with direction names\n1..10000 steps, total at most1080000 "
         "frames",
         schema(
             R"({"type":"array","minItems":1,"maxItems":10000,"items":{"type":"object","required":["frames"],"additionalProperties":false,"properties":{"frames":{"type":"integer","minimum":1,"maximum":1080000},"buttons":{"type":"array","items":{"type":"string"}},"stick":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number","minimum":-1,"maximum":1}}}}})"),
         R"([{"frames":30,"stick":[0,1]},{"frames":6,"buttons":["jump"]},{"frames":60}])", false},
        {"inputs-status", "Exact frame delivery and final Mario position/velocity/ground/dead state", "", schema(R"({"type":"object"})"), "{}",
         false},
        {"inputs-cancel", "Release all controls and cancel the input plan", "", schema(R"({"type":"object"})"), "{}", false},
    };
    return specs;
}

Json::Value helpJson(const Json::Value& args) {
    auto specJson = [](const CommandSpec& s, bool full) {
        Json::Value j = Json::Value::object();
        j.set("name", s.name);
        j.set("summary", s.summary);
        if (s.mutates)
            j.set("mutates", true);
        if (full) {
            if (!s.argsDoc.empty()) {
                Json::Value lines = Json::Value::array();
                std::size_t start = 0;
                while (start < s.argsDoc.size()) {
                    std::size_t end = s.argsDoc.find('\n', start);
                    if (end == std::string::npos)
                        end = s.argsDoc.size();
                    lines.push(s.argsDoc.substr(start, end - start));
                    start = end + 1;
                }
                j.set("args", std::move(lines));
            }
            if (s.schema.isObject() && !s.schema.members().empty())
                j.set("schema", s.schema);
            if (!s.example.empty()) {
                std::optional< Json::Value > example = Json::parse(s.example);
                if (example)
                    j.set("example", *example);
            }
        }
        return j;
    };
    auto find = [&](const std::vector< CommandSpec >& list, const std::string& name, const char* pWhat) -> const CommandSpec& {
        std::vector< std::string > names;
        for (const CommandSpec& s : list) {
            if (s.name == name)
                return s;
            names.push_back(s.name);
        }
        fail(std::string(pWhat) == "op" ? "bad-op" : "unknown-command", std::string("no ") + pWhat + " '" + name + "'", "", names);
    };
    Json::Value out = Json::Value::object();
    if (args.has("live")) {
        out.set("live_command", specJson(find(liveCommands(), args["live"].asString(), "live command"), true));
        return out;
    }
    if (args.has("command")) {
        out.set("command", specJson(find(commands(), args["command"].asString(), "command"), true));
        return out;
    }
    if (args.has("op")) {
        out.set("op", specJson(find(opKinds(), args["op"].asString(), "op"), true));
        out.set("refs", kRefDoc);
        return out;
    }
    const bool all = args["all"].asBool();
    Json::Value list = Json::Value::array();
    for (const CommandSpec& s : commands())
        list.push(specJson(s, all));
    out.set("commands", std::move(list));
    Json::Value ops = Json::Value::array();
    for (const CommandSpec& s : opKinds())
        ops.push(specJson(s, all));
    out.set("ops", std::move(ops));
    Json::Value live = Json::Value::array();
    for (const CommandSpec& s : liveCommands())
        live.push(specJson(s, all));
    out.set("live_commands", std::move(live));
    out.set("refs", kRefDoc);
    out.set("results", "every reply is {\"ok\": true, ...} or {\"ok\": false, \"error\": {code, message, hint, candidates, where}}");
    Json::Value codes = Json::Value::object();
    for (const auto& [code, meaning] : errorCodes())
        codes.set(code, meaning);
    out.set("error_codes", std::move(codes));
    out.set("more", "help {\"command\": name} or {\"op\": name} for args, schema and an example; {\"all\": true} for everything");
    return out;
}

}  // namespace PetariEditor::LevelApi
