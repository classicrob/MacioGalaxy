# Gravity implementation

`1` or **Gravity** in the toolbar shows the live fields. Shapes and flowing arrows
are drawn through the shared depth-tested debug renderer. Point, cube, cone,
disk, disk-torus, parallel (sphere, box and cylinder bounds), segment and wire
fields are read from the engine, including gravity created by other actors.
Inactive fields are faint. Selecting a field colours its outline gold.

The cursor probe queries the game's normal-gravity manager at the collision
surface under the cursor. It names the strongest field in the winning priority
group, reports the priority, and distinguishes equal-priority blends from lower
priorities. Its arrow is the engine's combined direction. Option probes at the
cursor's depth in air; empty sky uses a fixed inspection depth.

Red patches mark collision-surface samples that no active normal-gravity field
covers. The survey samples the visible ground incrementally and retains found
patches until the model changes. Checks identify affected ground rows. This is a
sampled survey, not a proof that an entire galaxy has complete coverage.

The Inspector changes shape, range, distant, priority and inverse through normal
undoable model operations. Move/rotate/scale uses the existing gizmo and updates
the actual field in previews, including cancellation. Wire shapes belong to their
linked path and are edited with the path handles. A wire shape needs an existing
path; shape conversion resets shape-specific arguments.

An in-place updater preserves manager slots for parameters and transforms.
Changing the gravity class recreates the actor and retires the old field, whose
slot remains occupied until Reload. The manager is fully re-sorted after priority
edits. Checks warn at 116 of 128 registered slots and report the full cap at 128.
Runtime checks available capacity before retiring a field that needs recreation.

Tests: `native_editor_gravity` covers meshes, model operations, undo, range
normalisation and Checks. `PETARI_EDITOR_GRAVITY_TEST=1` drives the live editor
through preview/cancel, priority/range changes, undo/redo, repeated updates and a
shape replacement; `PETARI_EDITOR_GRAVITY_SHOT` requests a screenshot.
