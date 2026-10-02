# Level editor mockup

`index.html` is a clickable mockup of the in-game level editor described in
[../UX.md](../UX.md). Open it in a browser (double-click it in Finder or `open
index.html`). It is one self-contained file: no network and no libraries.

Screens (the buttons along the top, or click around inside):

1. **Edit view**: the frozen game with the Outliner, the Inspector for a Star
   Bunny, its gizmo in the gravity frame, the play marker, and a switch wire.
2. **Object browser**: cards, scopes, details, "See an example", and a Goomba
   being dragged onto the planet.
3. **Properties & links**: every switch wire, and the popover that asks how a
   Launch Star should react to the switch it was just linked to.
4. **Gravity**: Space Junk's start block with its plane gravity box, flow
   arrows, a losing point gravity (hatched), a dead zone, the probe, a test fall,
   and the priority list.
5. **Camera**: a camera area, its fields, and the picture-in-picture preview.
6. **New galaxy**: the three-step sheet.
7. **Checks & changes**: problems with one-click fixes (try them), and the
   Changes tab.
8. **Share**: the share sheet; Export shows the result.
9. **Play-test**: panels away, the game full size. **Tab** anywhere toggles
   Edit and Play; coming back draws Mario's trail.

Also: the mission chips in the toolbar fade objects that are not in that mission,
the viewport's left-edge buttons (or keys 1–5) cycle what's drawn (off,
selection, all), hovering an Inspector field fills the help strip, and the
objects in the viewport are clickable.

The objects, fields and switches are real: they are the placements of
`PuffballZone`, the start planet of Gusty Garden mission 1, read from the disc
(for example CrystalCageS #6 turns on switch 13, and StarPieceSpot #20 appears on
it). The Launch Star #45 and switch 30 are an example edit.

## The viewport images

The viewport shows real frames of the game. They are game imagery, so they are
not in the repository; the page loads them from
`build/level-editor-ux/viewport-*.jpg` (relative path
`../../../../build/level-editor-ux/`) and paints a stand-in when they're missing.
To make them from your own disc:

```sh
docs/dev/level-editor/mockups/capture-viewports.sh
```

It runs three background stage smokes (Gusty Garden, Space Junk, Good Egg; a few
minutes each, through `build/locked-app.sh`) with the photo-mode test driver,
and keeps a HUD-less frame from just after photo mode is entered. Needs a built
app (`PETARI_APP=` to use another) and `build/game-data/RMGE01`.

The icons are drawn for the mockup. In the game, thumbnails are rendered from
the game's own models at runtime (UX.md section 7). The Galaxy lettering is
stood in for by the system's rounded font (`ui-rounded`); in the game, titles,
tabs and buttons use the disc's MessageFont26 and MenuFont64.
