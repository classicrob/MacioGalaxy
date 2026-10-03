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
row in the zone uses; `"@goomba.SW_DEAD"` takes another row's switch (in the same batch too), so
"defeat the Goomba, then the 1-Up appears" is two adds. Zone switches only connect rows of one zone
(refused otherwise). `links: {"CommonPath_ID": 0}`. `fields` sets any column by name.

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
`colliders` lists what counts as ground. Without `zone`, an `on_ground` add goes in the zone that owns
the ground it lands on, as the Browser places.

Headless, the ground is the collision of every row placed in the current mission: each class's
`ObjectData/<class>.arc` `.kcl` (read with the same prism reconstruction as `KCollisionServer::getPos`)
and terrain brushes (the terrain library's own brush mesh), transformed by the row and its zone.

Headless gravity is **approximate**: the mission's gravity rows with the shapes
`GravityCreator.cpp` builds from a row (500 units per unit of scale): point (its `Distant` sphere),
cube (a box standing on the row's origin), disk and disk-torus (radius from the largest scale,
one- or two-sided from `Obj_arg0`), segment (1000 x scale_y along the row's up), plane (a `Range`
sphere), plane in box and plane in cylinder; `Range`/`Distant`/`Inverse` as `GravityUtil.cpp` reads
them (negative keeps the default). The highest `Priority` wins, then the nearest (the engine blends
equal priorities); cones and wires are taken as their centre; with no field, toward the nearest
collision surface. Replies say
which field was used (`"gravity": "GlobalPointGravity EggStarGalaxy:common/planetobjinfo:4"`). Give
`down: [x,y,z]` to override, or use live mode for the engine's own answer.

Arrays snap each copy on their own: an `arc` of coins with `on_ground` hugs the planet; without it
the arc is a parabola between `from` and `to` whose middle is `peak` above the line, along -gravity.

## Verification

- `checks`: the in-game Checks, new problems only (`--all` adds the shipped ones, marked).
  Each has a stable `key`, `severity`, `message`, the rows, and `fix` when `{"op": "fix"}` can
  repair it.
- `diff`: rows added or changed against the disc, with `[disc, now]` cells.
- Live mode (below): the same ops in the running game, reload, screenshot, warp Mario, trace him.

## Live mode

The test server (`native/tools/test_server.py`, `PETARI_TEST_SERVER`) takes these commands from
`src/Game/Scene/TestServerEditor.cpp` (start it with `PETARI_EDITOR=1` for the editing ones):

| command | |
| --- | --- |
| `editor-begin` | load the editor's model for the running stage (no Edit nerve needed); ops then apply and SceneSync re-creates their actors |
| `editor-apply FILE` | low-level ops (`export`'s format, one op or `{"ops": [...]}`) queued as editor commands, each one undo step in the game |
| `editor-undo [n]`, `editor-redo [n]`, `editor-reload`, `editor-save MOD [--full]` | the editor's own commands |
| `editor-play [x y z [up] [front]]` | Play from a marker there: Mario teleported, upright for up |
| `editor-state`, `editor-checks` | model revision, history, mode; the game's Checks with refs |
| `ground x y z [dx dy dz]`, `gravity x y z` | the engine's own gravity and first map polygon (exact) |
| `mario`, `warp x y z`, `wait N` | Mario's position, gravity, ground, death; move him; let N frames run |
| `screenshot PNG` | the next presented frame, without the editor's overlay |

Replies are `ok <json>` / `error <text>`. `level_api_x.Live` wraps them: `Live.start(app=...)`,
`live.begin()`, `live.sync(level)` (brings the game to the workspace's journal: undoes what diverged,
applies what is new; refs resolve in the game because they are stable), `live.reload()`,
`live.play(pos, up)`, `live.warp(pos)`, `live.trace(frames)`, `live.ground(pos)`,
`live.screenshot(path)`. `examples/live_verify.py` runs example 1 that way.

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

## Examples

- `examples/sling_star_course.py`: Good Egg mission 1, from Mario's start: a coin trail on the
  planet, a Sling Star, a coin column above it and a Power Star at the top (12 objects, one batch).
- `examples/goomba_switch_garden.py`: on Soramame: a ring of coins, a Goomba whose defeat (a "new"
  switch) makes a 1-Up appear (`"@goomba.SW_DEAD"`), a grid of Star Bits, and a dry-run probe of a
  mistake showing the checks an agent gets back.
- `examples/live_verify.py`: example 1 in a running game (sync, reload, exact ground vs headless,
  screenshots, Mario dropped on the Sling Star and traced).

## Performance

Good Egg (18 zones, 1,724 rows), CPU time: opening the galaxy (catalog, disc mount, model, the
baseline checks) about 0.3 s; a Checks pass about 0.06 s; a batch about two Checks passes plus the
ops. A CLI call therefore costs about 0.3-0.4 s of CPU (wall time was 1-5 s while the machine was
loaded by other workers); `serve` and the Python wrapper pay the open once. The headless world loads
each class's collision once per process.

## Limits (known)

- Headless gravity is approximate (above). The ground is the collision of rows with a `.kcl` named
  after their class; objects whose collision comes from another archive (some map parts) are not
  ground headless (`colliders` and its `no_kcl` list show what is).
- No path, gravity-shape or brush-specific ops yet beyond raw fields: paths use their tables' rows
  by ref and `fields`; gravity rows take `args`/`fields`; brushes take their named args.
- Mission names come from ScenarioData (Japanese, internal); the pause-menu names need the game's messages.
- Route playback (a recorded input route) is not wired to the test server yet: verification uses
  warp + trace (positions over frames) instead.
- The workspace's base is the disc (plus the user's enabled mods with `--user-mods`): saving into a
  mod that is also enabled in the base mixes the two, as in the game.
