# Level editor: PR log

Each editor topic branch (`editor/<topic>`) is tested in a clean worktree and merged into
`level-editor` with `--no-ff`; this log records the stack. Nothing here is on `main` or pushed.
"Tests" are the integrator's clean-worktree run: all + petari_tests + petari build, then
`ctest -LE timing -E native_movie`.

| # | Topic | Base | Merge commit | Tests (clean worktree) |
|---|---|---|---|---|
| 1 | `editor/edit-api`: the UI <-> Runtime interface (`edit_api.hpp`) and session core; the user's decisions in TECH/SUMMARY | `level-editor` | `c5efd0c1f` | build ok; 110/110; `native_editor_api` |
| 2 | `editor/edit-model`: disc formats (Yaz0, RARC, BCSV) and the `EditModel` (stacked on `editor/edit-api`) | `level-editor` after PR 1 | `055938852` | build ok; 112/112; `native_editor_formats`, `native_editor_model` |
| 3 | `editor/dock-viewport`: spike S4, the docked viewport (ImGui dockspace, presenter content area, free aspect) | `level-editor` | `8e1570d3a` | build ok incl. the app; 113/113; `native_editor_ui`, `native_home_menu`, `native_movement_baseline` |
| 4 | `editor/camera-input`: Edit mode, the editor camera and the Tab toggle behind the Mods-page "Level editor" switch (stacked on dock-viewport and edit-api, both already in) | `level-editor` | `a46175439` | build ok incl. the app; 113/113; `native_input` (new testLevelEditor), `native_editor_ui`, `native_home_menu`, `native_movement_baseline` |
| 5 | `editor/runtime-hooks`: the spike's engine hooks, productised, off unless the editor is on (stacked on edit-model, already in) | `level-editor` | `364c38a7a` | build ok incl. the app; 113/113 |
| 6 | `editor/scene-sync`: SceneSync, the editor runtime in the frame (session, row/actor binding, command drain, live re-creation of changed rows); based on level-editor `424853073` | `level-editor` | `b7cf48af9` | build ok incl. the app; 113/113 |
| 7 | `editor/picking-outline`: picking, hover and selection in Edit mode, plus Runtime's wiring (stacked on camera-input) | `level-editor` | `6a957caa3` | build ok incl. the app; 113/113; two mechanical merge conflicts resolved by the integrator (below) |
| 8 | `editor/fast-reload`: reload from the model and Play (S3, ~0.3-0.5 s same-stage reload); stacked on scene-sync | `level-editor` (after merging main) | `ba71d24c2` | build ok incl. the app; 112/112 incl. `native_editor_model` |
| 9 | `editor/save-mod` + `editor/m2-api` (self-merged by the runtime worker): Full-archive mod save; retire no longer re-draws a clipped actor; the M2 interface (catalog API, add/delete ops, checks, missions); drops the temporary window-restore guard (main's fix is in) | `level-editor` (after PR 8) | (this merge) | merged tree: build ok incl. the app; `ctest -R "native_editor|native_input|native_home_menu|native_platform_dvd|native_mod_folder"` 9/9; live: `editor_save_smoke.py`, `editor_reload_smoke.py`, `editor_sync_smoke.py` PASS |

## Notes

- PR 1 and 2 were stacked (PR 2 contains PR 1's commit); both were merged in order, so the
  history shows two merges.
- PR 3 was independent of 1 and 2 and merged without conflicts.
- Unverified by the integrator (worker-reported or not run): real mouse input in the docked layout,
  a live splitter drag, a full-game run with the editor on; no game behaviour changes with the
  editor off (the movement baseline check and the stage smokes were the workers' runs).
- PR 4 and PR 5 were merged back to back; the combined tree (both merged) passed 113/113.
- The live `movement_baseline.py` (mods off/on) is not run by the integrator (it drives the app); the
  static `native_movement_baseline` check passes. The runtime worker saw the live baseline flake on a busy
  machine (A-tap timing): run it under `PETARI_LOCK_CLASS=quiet` (see the script's docstring).
- Live `movement_baseline.py --mods both` under `PETARI_LOCK_CLASS=quiet` (runtime worker): the
  `level-editor` tip at `424853073` (PRs 1-5) passes mods off and on; `editor/scene-sync`
  (merged with that tip) passes mods off and on.
- PR 6: the worker also ran editor_sync_smoke.py and editor_hooks_smoke.py (PASS) and the live movement_baseline under the quiet lock (off PASS, on PASS); the integrator did not run apps.
- PR 7 merge conflicts (both mechanical, resolved in the merge commit): root `CMakeLists.txt` (both sides added the same
  `target_link_libraries(petari_game_objects PRIVATE petari_editor_core)`; kept scene-sync's block, which includes it)
  and `GameScene::init` (kept both independent lines: `Hooks::syncSwitch(...)` from scene-sync, then
  `EditorCamera::abandon()` from picking-outline). Owners should glance at them.
- Main was merged into level-editor first (`dd2713608`: never-restore-windows fix `3df2658a4`, `native_audio_sdl` labelled timing), no conflicts.
  PR 8 carries a TEMPORARY registerDefaults for window state in background smoke mode (its author's note); it is redundant with main's
  fix now and should be dropped by its owner.
