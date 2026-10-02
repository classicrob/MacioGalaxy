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
