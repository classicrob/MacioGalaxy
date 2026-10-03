# Areas and camera previews

Areas and camera volumes use the same row selection, transform gizmo and undo history as objects.
`2` cycles area visibility and `3` cycles camera visibility between selection, all and hidden.
Click a visible outline to select it; the filled interior does not intercept object picks.
The Inspector shows dimensions in game units and names the known camera, death, water,
message and switch arguments. Boxes keep the game's centred/base-origin distinction;
spheres, lower hemispheres and cylinders use their actual game dimensions.

Ordinary death, water, message and switch areas can update their bounds and verified runtime
arguments live. A deleted live area is deactivated; undo restores it. New supported areas
require room in their manager. Camera areas and classes with cached or host-dependent state
remain pending until Reload. Their overlays show the edited model immediately.

Selecting a camera area opens a corner preview of scene models. Choose the play marker
(the area centre is used when the marker is outside) or Mario as the target. The temporary
camera reads the current `.bcam` row and zone transform; parameter edits update the preview
while the actual game camera remains unchanged until Reload. Distance, angles, roll and
offsets retain the camera format's units. Where the table has no field-of-view override flag,
the field-of-view control is disabled because the game uses the galaxy default.

The preview currently supports 14 camera families whose pose comes from their target and
parameters: parallel, tower, follow, wonder planet, fixed point, fixed eye, slider, inward
tower/sphere, tripod planet, ground, 2D slide and water follow/planet. Cameras requiring boss,
register, animation or other live context display a Reload/Play notice. Effects, shadows and
postprocessing are outside this models-only preview. Readbacks reuse one ImGui texture.
Updates are limited to 10 Hz, hidden/offscreen models are omitted, and a view that exceeds the
shared editor draw budget retains its last still with an explanation.

Checks run in both the game and headless model loader: overlapping camera areas sharing
priority and player category, missing camera parameters, a death area at or immediately below
a start, and per-manager area capacity. Camera areas requiring more than 1,024 distinct chunks
also report an error; this is a lower-bound check, not a complete census of event cameras.
The game's existing issues are the usual Checks baseline.

Runtime maps area placement table/ordinal pairs to stable RowIds when the scene binds, so
inserting or deleting another row cannot redirect a hover or edit to a neighbour. The
NameObj registry is separate from LiveActor bindings. Camera files participate in the normal
model, archive save, patch, undo and reload paths.

Validation:

- `native_editor_areas`: shape dimensions, meshes, rotated-volume overlap, 405 box intersection
  comparisons, camera/death/capacity checks, `.bcam` save/patch and undo.
- `native_editor_formats`: all 230 disc `.bcam` files and all other stage tables rebuild
  byte-for-byte when unedited.
- `native/tools/editor_areas_smoke.py --app <Petari> --output <private-directory>`:
  live bounds/size, delete/undo, stable placement IDs, camera pending state, parameter preview,
  active-camera restoration and Reload preserving history. The runner removes its own cache.
