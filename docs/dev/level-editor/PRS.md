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

## Notes

- PR 1 and 2 were stacked (PR 2 contains PR 1's commit); both were merged in order, so the
  history shows two merges.
- PR 3 was independent of 1 and 2 and merged without conflicts.
- Unverified by the integrator (worker-reported or not run): real mouse input in the docked layout,
  a live splitter drag, a full-game run with the editor on; no game behaviour changes with the
  editor off (the movement baseline check and the stage smokes were the workers' runs).
