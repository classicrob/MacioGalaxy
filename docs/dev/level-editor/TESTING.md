# Faster live tests: warm seed, fast boot, test server

Three layers, cheapest first. All are test-only; none changes normal play.

1. **Warm shader seed** (`native/tools/warm_seed.py`): no cold pipeline compiles. `eval $(native/tools/warm_seed.py env --app <Petari>)`
   exports `PETARI_SHADER_SEED_DIR` (publishing a per-build seed first if missing); `warm_seed.py prepare --app <Petari> --user <dir>`
   clones the warm databases into a run's user dir. Details: `native/tools/STAGE_SWEEP.md`.
2. **Fast boot** (`PETARI_FAST_BOOT=1`): the strap-reminder button is accepted after 30 frames instead of 450 (and tapped at 40), the stage-start
   fly-over camera is skipped, and `stage_sweep.py`-based tools set `PETARI_STAGE_GATE_MS=0`. Leave it off for the movement baseline and any run that
   checks the title/file-select flow or the stage-start camera.
3. **Test server** (`PETARI_TEST_SERVER=<dir>`, client `native/tools/test_server.py`): one background app stays up; commands are files, so a worker's
   smokes reuse one process.

## Measured (Good Egg, launch to first normal-play frame, one worker's machine, other apps running)

| | seconds |
| --- | --- |
| cold (no seed, no fast boot) | ~168 (includes contention) |
| warm seed | 47-51 |
| + fast boot strap and gates | 40-42 |
| + stage-start camera skipped | 24 |
| test server, second and later tests | see below |

## Test server

```sh
APP=build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari
native/tools/test_server.py start --app $APP --dir build/test-server/me       # boots once, returns when the stage answers
native/tools/test_server.py send  --dir build/test-server/me status
native/tools/test_server.py send  --dir build/test-server/me warp OceanRingGalaxy 1   # no reboot
native/tools/test_server.py send  --dir build/test-server/me reload                   # same-stage restart, ~0.4 s
native/tools/test_server.py stop  --dir build/test-server/me
```

Commands (`src/Game/Scene/TestServer.cpp`, run on the game thread from `GameScene::update`): `status`, `warp <Stage> [n]`
(the Level Select warp path), `reload [start]` (`PetariEditor::Runtime::reloadScene`), `quit`. Replies: `ok ...` or `error ...`.
The server holds one app slot until stopped: stop it when you are done. While it is up the smoke's movement script is skipped (it idles) and
the run no longer ends by itself (`--lifetime` alarm, default 2 h).

## Before any live smoke: is the app really relinked?

After a merge or any source change, build the app explicitly (`cmake --build build/macos-gx -j6 --target petari`, plus your test
targets). A plain full build has twice ended with "[100%] Built target ..." and no error while `Petari.app/Contents/MacOS/Petari` was
still the old binary, so smokes passed on stale code. Then check the binary is newer than your last merge or commit:

```sh
stat -f %Sm build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari   # must be after:
git log -1 --format=%cd
```

Read the build's tail; grepping for "error" alone misses an interrupted build.

## Disk: every run leaves a ~1.9 GB private cache

Delete a finished run's `<run>-user/cache` (keep logs and shots), or run `build/prune-run-artifacts-caches.sh` in the main checkout
(removes run caches older than 30 min across main and all worktrees, skipping open files). `class_sweep.py` and `test_server.py stop` delete their
own; `warm_seed.py` keeps only the newest 3 seeds.

## Frame-exact input through the test server

```sh
native/tools/test_server.py input --dir <dir> [--log walk.csv] --steps '[{"frames":90,"stick":[0,1]},{"frames":8,"buttons":["A"],"press":true},{"frames":30}]'
native/tools/test_server.py route --dir <dir> <PETARI_ROUTE_RECORD.csv> [--log f.csv] [--from N] [--frames N]
native/tools/test_server.py send  --dir <dir> warp <Stage> [<scenario>]     # unknown stage or scenario: "error ..."
```

One step is applied per game frame (the stick as an analog pad axis, y up; buttons through their bindings), so a run does not depend on the OS or
the machine's load. `press: true` taps for the first 6 frames of the step, else the buttons are held for the whole step. The reply is Mario's
position, velocity, grounded and dead flags at the end (`ok ... ended=done`, or `error ... ended=stage-changed|died|control-lost|player-lost|cancelled`);
`--log` writes one row per frame. Buttons: A B Z Spin Cap CamLeft CamRight Plus Minus. The engine is `src/Game/Scene/TestServerInput.*`; code
callers use `TestServerInput::startSteps/status/cancel` from the game thread.


## Static model imports

`native_editor_import` generates its own CC0 OBJ, textured glTF/GLB and image fixtures.
It checks orientation, UV transforms, compressed buffers, GX encoders, actual J3D loading,
collision simplification/splitting, neutral asset round trips, reusable multi-material
placement, native planet baking and idempotent patch builds. No downloaded model is needed.

`native/tools/editor_import_smoke.py --app <binary> --output <unique-run>` creates a private
stage fixture and CC0 checker cube, opens the import sheet, places it, checks the game's
collision ray, undo/redo, patch assets and full native bake. It captures the sheet and model.
Then use `--reopen-mod <first-run>/user/mods/ImportedFixture` with a new output directory to
check editable reopening; add `--play-only` for editor-off loading as a native planet.
Every run records source/binary hashes, keeps its logs/screenshots and removes its own cache.

### Creating new paths

`native/tools/editor_new_path_smoke.py --app <Petari executable> --output <unique directory>`
uses a private Good Egg stage fixture. It drives the same path controller as the UI using
real game ground rays: three points, cancel without a model edit, create, one-step undo/redo,
then a fresh Launch Star with its own start/landing rail and atomic link. It saves the patch
and captures `new-path-draft` / `new-path-created`; the source/app hashes are in `revision.txt`.
`native_editor_paths` additionally rebuilds a saved three-point rail from its patch. The
background test checks authoring and linking, not completion of Mario's flight.

### Array direction and framing

The existing `editor_array_smoke.py` now includes a three-copy line at spacing 850: Right
follows the local gravity frame and its end handle stays inside the viewport. Up preserves
elevation and aligns the copy forward axes along gravity; neither preview changes the model.
The run then continues through all five layouts, apply, undo/redo and area slot reuse.
`array-axis` captures the visual picker and framed preview. `native_editor_array` covers all
six local directions and preview bounds including original objects, copies and the handle.

### Clean Browser donor placement

`native/tools/editor_placement_smoke.py --app <Petari binary> --output <private output>`
uses a private Good Egg fixture and the Browser's placement request. It sets
SW_APPEAR/SW_SLEEP on a 1-Up donor, places a clean copy, checks its live model,
undo/redo, and explicit Keep links. A Launch Star donor with SW_APPEAR1007 checks
that optional wiring clears while its required path and zone remain. This tests
the placement request and game adapter; it does not simulate clicking the Browser.
The runner records source and binary hashes, captures both new objects, stops its
server and removes its fixture cache. `native_editor_placement` covers catalog
metadata, typed link resets, donor preservation, and destination-layer visibility.

### Editor help

`native/tools/editor_help_smoke.py --app <Petari> --output build/<your-help-run>`
uses a private Good Egg fixture and the real SDL toolbar-click path. It opens Keys and tips,
records the overlay and the six-mission toolbar at the default 1280x720 window size, closes
help and returns to play. Require both `[editor-help]` PASS lines and the stage PASS, then
visually inspect the captures. The runner removes its cache. `native_editor_ui` separately
checks the question-mark/typing/modifier gates, Escape ownership without cancelling tools,
non-modal bounds and Focus-mode drawing. Screenshots remain in `build/`, not Git.
