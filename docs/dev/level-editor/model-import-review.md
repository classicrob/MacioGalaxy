# M6 model import: ready for review

Implementation: `editor/model-import`. The executable verified below was built at
`de8829bf7199b715d8e8a72a03064b6af1c2c2dc`; subsequent documentation does not change it.
Binary SHA-256: `c95ab07becbe6b102a16788aa326dce609e1635259f3e3e5594d1edf8b698737`.

The sheet imports OBJ/glTF/GLB, previews orientation/scale/origin and textures, maps materials
and collision surfaces, simplifies/splits collision and creates live reusable parts in one
undo step. Neutral assets travel with patch mods; the loader bakes native planets when the
editor is off. Static/rest geometry and base-colour materials are supported; animation and
full PBR shading are not imported.

## Review boundaries

- Runtime: `GeneratedRowProvider` in runtime.hpp/session.cpp; TerrainSync moved onto it;
  ImportSync lifecycle and live geometry; Assets provider registry and ModelIO integration.
  `ClassInfo::bakesScale` replaces the brush-name exception in the uneven-collision Check.
- UI: import.cpp, reusable Host texture slots and mutex dialog callback; tagged Browser,
  editor menu, drawFrame and file-drop calls. EditorGizmo appends all material splits when
  placing the primary imported Browser card.
- Terrain: `Part::setCollision(nullptr, nullptr)` invalidates old collision and supports
  visual-only imported parts. The KCL and BMD writers themselves are unchanged.

Review requests were sent to Runtime and UI, then to the lead and integrator for substitute
review. All four threads reported `error` on 2026-10-03. This branch has **not been merged**;
there is no merged PRS row yet. Resolve the review, merge any newer level-editor changes,
run the prescribed incremental build/targeted tests, add the PRS row and fast-forward via
the shared helper.

## Verification

All paths below are in `petari-worktrees/editor-paths`:

- `build/import-final-verified-tests.log`: 5/5 suites pass: import (57 checks), model,
  catalog, save and UI. Import tests generate CC0 fixtures and exercise the actual J3D
  loader for CMPR/RGB5A3/I8 materials, glTF texture transforms/compression, collision
  splitting/simplification, portable asset round trips and native/patch baking.
- `build/import-live-final/run.log`: sheet placement, actual game collision ray hit, one
  undo step, no reload, dead/undrawn model on undo, reusable part on redo, patch asset save
  and Full-archive bake pass; ordinary stage checks and 1800 idle frames pass.
- `build/import-reopen-final/run.log`: saved patch reopens as an editable textured part with
  collision; stage checks and 400 idle frames pass.
- `build/import-editor-off-final/run.log`: loader builds and opens the native planet plus
  Middle/Low archives; placement trace confirms the EditorImport planet; stage checks and
  400 idle frames pass with the editor off.
- Visually inspected `build/import-live-final/surfaces/surface-import-sheet-5.png` and the
  placed checker cube captures. The panel fits the window and keeps its actions visible.
- Every final run has matching source/binary hashes in `revision.txt`. All test apps exited
  successfully and their private run caches were removed.

The native OS file picker is not exercised by the background smoke; it uses the previously
landed Host dialog seam. The smoke opens the sheet with a generated fixture path.
