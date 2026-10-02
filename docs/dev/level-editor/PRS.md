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
| 9 | `editor/outline` (self-merged; the UI's M1 stack: `editor/gizmo`, `editor/inspector`, `editor/play-toolbar`, `editor/outline`): the transform gizmo (gravity frame, slide on the ground, End to drop, one op per gesture), the raw-field Inspector with Outliner (by zone and kind) / Changes / History and undo/redo, the play loop through Runtime (Tab plays from the marker, Shift+Tab resumes, Option+Tab from the start; M places the marker; Reload pill, memory meter; the selection kept across play and reload), and S5's selection outline (screen space, EFB alpha) | `level-editor` | this merge | full build (ccache) ok incl. the app; ctest `editor|input|home_menu|seam|galaxy_font|movement_baseline` 12/12; live Gusty Garden smokes on this merge: editor on (gizmo move + drop, marker, play, back to Edit, play) PASS, editor off PASS |
| 10 | `editor/save-mod` + `editor/m2-api` (self-merged by the runtime worker): Full-archive mod save; retire no longer re-draws a clipped actor; the M2 interface (catalog API, add/delete ops, checks, missions); drops the temporary window-restore guard (main's fix is in) | `level-editor` (after PR 9) | (this merge) | merged tree: build ok incl. the app; `ctest -R "native_editor|native_input|native_home_menu|native_platform_dvd|native_mod_folder"` 10/10; live: `editor_save_smoke.py`, `editor_reload_smoke.py`, `editor_sync_smoke.py` PASS; `native_editor_gizmo`, `native_editor_ui` pass; `movement_baseline.py --mods both` (quiet) PASS |
| 11 | `editor/structural-ops` (self-merged by the runtime worker): add and delete rows live with undo (stable row ids, duplicate/copy, new layer tables, tombstones), the ArchiveHolder capacity guard, host allocation scopes, and the frozen Edit scene carrying out connect/disconnect requests (the "double bunny") | `level-editor` (after PR 10) | (this merge) | merged tree: build ok incl. the app; `ctest -R "native_editor|native_input|native_home_menu|native_platform_dvd|native_mod_folder"` 10/10; live: reload (incl. the frozen draw-list check), sync (live duplicate/delete), hooks, save smokes PASS |
| 12 | `editor/runtime-api` (self-merged): `Runtime::reloadScene` / `reloading` for test drivers (the class sweep's test server) | `level-editor` (after PR 11) | (this merge) | build ok incl. the app; targeted ctest; live: `editor_reload_smoke.py` (now with a plain reload, editor off: 362 ms) PASS |
| 13 | `editor/terrain-kcl` (self-merged; spikes S6 and S7, TECH.md 7.1): the terrain library (`native/editor/terrain`: brush meshes, the KCL + .pa writer, the J3D `.bmd` writer with a material cloned from a disc model), live terrain parts (collision from an in-memory KCL; the model served as an in-memory archive and drawn by a ModelObj), `petari_terrain_bake` (planet archives + PlanetMapDataTable + placement/gravity rows as a Full-archive mod), the native `FileHolder` fix for files not on the disc, and generated archives through Runtime's archive overrides (`FileLoader::requestMountArchive`) | `level-editor` | this merge | full build (ccache) ok incl. the app; ctest `native_editor|kcl|j3d|mod_folder|platform_dvd|stage_edit` all pass (new: `native_editor_terrain_kcl`, `native_editor_terrain_model`); live: `editor_terrain_smoke.py` box/sphere live and baked PASS (Mario walks ~800 units on each), `editor_hooks_smoke.py` (editor off unchanged, on) PASS, `editor_reload_smoke.py` PASS |
| 14 | `editor/galaxy-registry` (self-merged; spike S8, TECH.md 5.7 and new-galaxies.md; stacked on terrain-kcl): the mod galaxy registry (`native/platform/dvd/mod_galaxy.cpp`, `<mod>/galaxy.json`), native guards for galaxies outside the compiled-in tables (message overlay, null event flags, `JMapInfoIter::getValue` past its table, borrowed music, the parser's accept/save-key/64 checks, SpinDriverPathStorage 64), `petari_terrain_bake --galaxy`, the stage smoke's `PETARI_STAGE_FOLLOW`, and the crash-site survey | `level-editor` | this merge | full build ok incl. the app; ctest `native_editor|kcl|j3d|mod_folder|mod_galaxy|platform_dvd|stage_edit|movement_baseline` all pass; live: `editor_galaxy_smoke.py` (enter, collect the star, return to the observatory, save; after a restart the star is owned) PASS, `editor_hooks_smoke.py` (editor off unchanged, on) PASS |
| 15 | `editor/terrain-shading` (self-merged): the terrain model writer bakes a light term into per-vertex CLR0 (disc planet `_v` materials are lit by vertex colour, so brushes were flat) | `level-editor` | this merge | build ok incl. the app; `native_editor_terrain_model` (36 models, baked light checked) and `native_editor_terrain_kcl` pass; live `editor_terrain_smoke.py` box (live) and sphere (baked) PASS |

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
