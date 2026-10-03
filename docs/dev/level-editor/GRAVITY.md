# Gravity implementation

`1` or **Gravity** in the toolbar shows the live fields. Shapes and flowing arrows
are drawn through the shared depth-tested debug renderer. Point, cube, cone,
disk, disk-torus, parallel (sphere, box and cylinder bounds), segment and wire
fields are read from the engine, including gravity created by other actors.
Each field has a stable hue. Ordinary fields draw outlines only; hover and selection
add a very faint fill and a stronger outline, while other fields recede. This avoids
washing out the scene when many volumes overlap. Flow uses at most 32 spaced
arrows, sized for viewing distance and fading through the last quarter of finite
range. A field emits arrows only where the engine identifies it as the winner;
surface samples show the actual combined direction. Hover labels show type,
priority and range.

The cursor probe queries the game's normal-gravity manager at the collision
surface under the cursor. It names the strongest field in the winning priority
group, reports the priority, and distinguishes equal-priority blends from lower
priorities. Its arrow is the engine's combined direction. Option probes at the
cursor's depth in air; empty sky uses a fixed inspection depth.

Red patches mark collision-surface samples that no active normal-gravity field
covers. The survey samples the visible ground incrementally and retains found
patches until the model changes. Checks identify affected ground rows. This is a
sampled survey, not a proof that an entire galaxy has complete coverage. Holes
sampled before the first edit form a local Checks baseline (their red markers
remain visible). An unseen vanilla hole discovered after editing may still be
reported; changing gravity can affect ground far from the edited field.

The Inspector changes shape, range, distant, priority and inverse through normal
undoable model operations. Move/rotate/scale uses the existing gizmo and updates
the actual field in previews, including cancellation. Wire shapes belong to their
linked path and are edited with the path handles. A wire shape needs an existing
path with at least two points; shape conversion resets shape-specific arguments.

An in-place updater preserves manager slots for parameters and transforms.
Changing the gravity class recreates the actor and retires the old field, whose
slot remains occupied until Reload. The manager is fully re-sorted after priority
edits. Checks warn at 116 of 128 registered slots and report the full cap at 128.
Runtime checks available capacity before retiring a field that needs recreation.

Tests: `native_editor_gravity` covers meshes, model operations, undo, range
normalisation and Checks. `PETARI_EDITOR_GRAVITY_TEST=1` drives the live editor
through preview/cancel, priority/range changes, undo/redo, 100 in-place updates,
all eight shapes, a linked wire point edit, uncovered-ground Checks and a blocked
replacement at the 128-slot cap. `native/tools/editor_gravity_smoke.py` runs it
in an isolated fixture and captures the Edit-mode window and game screenshots.
Use `--capture-wide` on that runner for unedited starter-planet and cube/disk
views at a wider editing distance.
The test server option `--warm-current-stage` warms only the requested stage; its
default still warms the standard five stages.

Headless game-thread callers use `EditorGravity::query` and `queryGround`.
For a ground hit, `Probe.groundPosition` and `groundNormal` are the exact collision
hit and face normal; `position` is lifted four units along the normal for the
gravity sample. Clearance is measured from `groundPosition`. Orientation uses
`Gizmo::gravityFrame` or `alignUp` with the negative gravity direction.
