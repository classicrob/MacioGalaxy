# Agent level authoring: petari-level-x

`petari-level-x` builds and edits Super Mario Galaxy levels from the command line, as JSON in and
JSON out. `native/tools/level_api_x` wraps it in Python. It is meant for agents and scripting
humans. Computer use stays the test of whether a human can use the editor.

It is the in-game editor's own core, linked without the game or a renderer:

| | the API uses |
| --- | --- |
| loading | `Runtime::loadEditModel`: the disc mounted through `petari_platform_dvd`, with the user's enabled mods if asked |
| state, ops, undo | `EditModel` (`apply`, `undo`, `redo`, row ids, `patch()`) |
| names, args, links | the object `Catalog`: curated names, enum labels, required links, presets |
| problems | `computeChecks`, with the shipped galaxy's own problems as the baseline (as the in-game Checks) |
| output | `Runtime::saveMod`: a patch mod (`edits/<Galaxy>.json`, no game data) or Full archives |

So an edit made here has the same semantics, validation and output as the same edit made in the
game. stage_edit.py is superseded.

## Quick start

```sh
X=build/macos-gx/native/editor/level_api/petari-level-x
$X help                                                    # every command and op, error codes, refs
$X --ws build/ws/egg init --disc build/game-data/RMGE01 --galaxy EggStarGalaxy
$X --ws build/ws/egg catalog "launch star"
$X --ws build/ws/egg class "Power Star"                    # args by name, enum choices, links
$X --ws build/ws/egg list --class Coin --near 0,0,0 --radius 3000
$X --ws build/ws/egg apply '[{"op":"add","class":"Coin","pos":[0,1500,0],"on_ground":true,"height":120,"as":"c1"}]'
$X --ws build/ws/egg checks
$X --ws build/ws/egg save "My Good Egg"                    # <ws>/user/mods/My Good Egg/edits/EggStarGalaxy.json
```

Python:

```python
import sys; sys.path.insert(0, "native/tools")
from level_api_x import Level

lv = Level.init("build/ws/egg", disc="build/game-data/RMGE01", galaxy="EggStarGalaxy", force=True)
with lv.batch(key="arc", label="Coin arc") as b:
    b.array("Coin", "arc", start=[0, 1200, 0], end=[1500, 1200, 0], peak=600, count=8)
    b.add("PowerStar", [1800, 1500, 0], on_ground=True, height=200, args={"Star ID": 1}, label="star")
print(lv.checks())
lv.save("Coin Arc")
```

## Concepts

**Workspace.** `--ws DIR` (default `$PETARI_LEVEL_WS`, else `./level-ws`) holds `level.json`: the
disc, galaxy, mission and user directory it was opened with, and the journal. Each CLI call opens
the galaxy, replays the journal, runs the command, and writes the journal back after a mutating
command. `serve` keeps one process and takes JSON lines (the Python wrapper uses it).

**Journal.** Each applied batch is one entry: one `EditModel` op, so one undo step. Entries are
low-level ops in JSON (`export` prints them) with stable refs, so they replay after a restart and a
running game accepts them unchanged (live mode).

**Refs** name one row and stay valid across undo, reload and restarts:

| form | meaning |
| --- | --- |
| `EggStarGalaxy:common/objinfo:12` | an original row: zone, table (without `jmp/placement/`), its index on the disc |
| `new:3` | the 3rd row this workspace added |
| `@star` | a label given with `"as": "star"` (add, duplicate, array, label) |
| `PowerStar#0` | the n-th row of a class, galaxy order |
| `{"class": "Coin", "near": [0,0,0]}` | a selector: class, zone, category, mission, table, state, text, near, radius, nth |

A selector must match exactly one row (else `ambiguous-ref` with candidates), unless an op takes
several (`select`, which may carry `"expect": n` as a guard).

**Coordinates.** `pos` and `rot` are world space; `rot` is degrees in the game's convention
(Rz·Ry·Rx, the BCSV `dir_*`). Rows store zone space; the API converts with the zone's placement in
the current mission. `zone_pos` / `zone_rot` read and write the row's own values. `up`/`front`
orient by direction instead of angles; `turn` spins about the object's own up.

**Args by name.** `args` takes the catalog's names (`"Star ID": 1`), enum labels
(`"Shadow type": "Surface circle"`), `true`/`false` for bools (written 1 / -1: the game's bool
getter treats anything but -1 as on), `null` for the class default (-1), or raw `Obj_arg0..7`.
Reading an object shows the same names; `null` means "the class's default".

**Switches and links.** `switches: {"SW_APPEAR": 12}`; `"new"` picks the lowest switch (0-127) no
row in the zone uses. `links: {"CommonPath_ID": 0}`. `fields` sets any column by name.

## Batches: transactional, idempotent

`apply` takes `{"ops": [...], "key": "...", "label": "...", "dry_run": false}` (or a bare array).

- **One transaction, one undo step.** Ops run in order against the model (so later ops see earlier
  ones: add, then duplicate the new row by its label). At the end the net change is merged into one
  op (adds with every cell, sets, deletes), the steps are backed out, and the merged op is applied:
  one history entry, as a gizmo drag is one entry in the game.
- **All or nothing.** The first failing op rolls everything back (labels too) and the error says
  which op and key (`where: "ops[2].args.Star IDD"`).
- **Idempotent retries.** A batch with a `key` that is already applied (and not undone) is skipped
  with `"skipped": true`. Agents should give every batch a key.
- **Dry run.** `dry_run: true` validates and reports `created`, `changed`, `deleted`, `placements`
  and `checks_introduced` without committing.
- **What changed.** The reply lists created objects (ref, class, world pos), changed and deleted
  refs, `placements` (where `on_ground` landed and on what), and `checks_introduced` /
  `checks_resolved`, so the agent knows at once if it broke something.

### Ops

`help --op NAME` prints each op's keys, schema and an example.

| op | does |
| --- | --- |
| `add` | place a class (`class` or `like` a row to copy, as the Browser does), in `zone`, `layer` or the layer active in exactly `missions`, with `preset` and every edit key |
| `set` (`move`, `rotate`, `scale`, `transform`) | change `ref` / `refs` / `select` rows |
| `duplicate` | copy a row (every field), then edit the copy |
| `delete`, `revert` | delete rows; put rows back to the disc |
| `array` | many adds along a `line`, `arc`, `ring`, `grid` or `path` (polyline) |
| `fix` | apply a check's automatic fix by its key |
| `label` | name an existing row |
| `raw` | a low-level op (the journal's format) |

Edit keys (add, set, duplicate, array): `pos`, `zone_pos`, `by`, `rot`, `zone_rot`, `up`, `front`,
`turn`, `scale`, `args`, `switches`, `links`, `fields`, `on_ground`, `height`, `down`, `align`.

## Placing on the ground

`on_ground: true` drops the point onto the ground below it along gravity; `height: N` keeps it N
units above, along gravity; the object is stood up against gravity (`align`, on by default with
`on_ground`). `ground {pos}` and `gravity {pos}` answer the same questions without editing, and
`colliders` lists what counts as ground.

Headless, the ground is the collision of every row placed in the current mission: each class's
`ObjectData/<class>.arc` `.kcl` (read with the same prism reconstruction as `KCollisionServer::getPos`)
and terrain brushes (the terrain library's own brush mesh), transformed by the row and its zone.

Headless gravity is **approximate**: the gravity rows of the mission (point: toward the centre;
cube: toward the nearest point of a scale x 500 half-size box; plane/parallel: the row's -Y;
segment, disk, cone and torus as their centre), filtered by `Range` and `Distant`, highest
`Priority` first, then nearest; with no field, toward the nearest collision surface. Replies say
which field was used (`"gravity": "GlobalPointGravity EggStarGalaxy:common/planetobjinfo:4"`). Give
`down: [x,y,z]` to override, or use live mode for the engine's own answer.

Arrays snap each copy on their own: an `arc` of coins with `on_ground` hugs the planet; without it
the arc is a parabola between `from` and `to` whose middle is `peak` above the line, along -gravity.

## Verification

- `checks`: the in-game Checks, new problems only (`--all` adds the shipped ones, marked).
  Each has a stable `key`, `severity`, `message`, the rows, and `fix` when `{"op": "fix"}` can
  repair it.
- `diff`: rows added or changed against the disc, with `[disc, now]` cells.
- Live mode (below): reload the running game, screenshot it, warp Mario, run a route.

## Errors

Every reply is `{"ok": true, ...}` or `{"ok": false, "error": {"code", "message", "hint",
"candidates", "where"}}`; the CLI exits 1 on an error, 2 on a usage error. `help` lists the codes.
Candidates are what to try: the nearest class names for a typo (`Gooomba` -> `Kuribo (Goomba)`),
a class's arg names, enum labels, allowed keys (`position` -> `pos`), zones, labels, the selector's
matches with their positions.

## Commands

| command | |
| --- | --- |
| `help [command] [--op NAME] [--all]` | the self-description; no disc needed |
| `init --disc D --galaxy G [--mission N] [--user DIR] [--user-mods] [--force]` | create a workspace (CLI) |
| `info` | missions, zones (placed, origin), counts, history, unsaved, new problems, labels |
| `galaxies` | galaxy names on the disc |
| `catalog [query] [--category] [--placed-here] [--limit]` | search classes |
| `class NAME` | one class in full, with examples placed here |
| `list [filters] [--full] [--limit] [--offset]` | objects |
| `get REF` / `get --refs [...]` | objects in full: args by name, switches, links, every field |
| `apply FILE\|-\|JSON` | a batch |
| `undo [--steps N]`, `redo`, `history` | the journal |
| `checks [--all]`, `diff` | verification |
| `ground --pos`, `gravity --pos`, `colliders` | placement queries |
| `save MOD [--full]` | write `<user>/mods/MOD` and turn it on for the next start |
| `export [--from] [--to]` | the journal's low-level ops |
| `serve` | JSON lines on stdin: `{"id": 1, "cmd": "list", "args": {...}}` (CLI) |

`--key value` flags parse as JSON when they can (`--mission 2`, `--near [0,0,0]`), `x,y,z` as a
vector, else as text; a `{...}` argument merges into the args.

## Performance

Measured on Good Egg (two zones, about 1,400 rows): see the PR. The CLI reloads the galaxy and replays
the journal on each call; `serve` (and the Python wrapper) load once.

## Limits (known)

- Headless gravity is approximate (above). The ground is the collision of rows with a `.kcl` named
  after their class; objects whose collision comes from another archive (some map parts) are not
  ground headless (`colliders` and its `no_kcl` list show what is).
- No path, gravity-shape or brush-specific ops yet beyond raw fields: paths use their tables' rows
  by ref and `fields`; gravity rows take `args`/`fields`; brushes take their named args.
- Mission names come from ScenarioData (the game's message names need the game).
- The workspace's base is the disc (plus the user's enabled mods with `--user-mods`): saving into a
  mod that is also enabled in the base mixes the two, as in the game.
