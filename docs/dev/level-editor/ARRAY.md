# Object arrays

Select one or more objects and choose **Array...** in the Inspector's Arrange section,
or press **Cmd+Shift+D**. The original selection stays in place. The panel controls
new copies; previews never create actors or edit the model.

- **Line:** copies one spacing apart along the selection's gravity-forward direction.
  Drag the end dot to change the count.
- **Arc / Ring:** radius, count, arc sweep and heading (keep, along, outward or inward).
  The centre is the selection pivot or the play marker. Drag the dot to change radius.
- **Grid:** rows and columns, using the gravity frame's right and forward directions.
- **Along a path:** select an existing rail, then use a count or a spacing and a starting
  distance. Open paths include both ends in count mode; closed paths omit the duplicate
  seam. Spacing follows accumulated curve length, including transformed child-zone rails.
  Align to path uses the full 3D tangent, including vertical segments.

**Snap each copy to ground** probes along gravity independently for each copy and keeps
its source's height above the surface. If a probe misses, the copy remains at the preview
position and the panel reports it. Groups preserve their offsets from the primary object.
Arrays are limited to 256 new objects, including every object in a selected group.

The preview draws translucent copies of the source model, without moving or creating any
actors. Missing, clipped or over-budget models use wire bounds. All draws share the editor
frame budget. Apply commits one compound operation, selects the copies, and preserves their
settings and mission layers. One undo removes the whole array. Selecting another object
applies the current preview; Esc or Cancel discards it. Leaving Edit discards the preview.

Validation is in `native_editor_array` (geometry, curve length, child-zone transforms,
settings and compound undo/redo) and `native/tools/editor_array_smoke.py` (all five previews,
live copies matching the preview, source unchanged, actor retirement on undo, stable redo).
