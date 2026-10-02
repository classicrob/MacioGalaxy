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
