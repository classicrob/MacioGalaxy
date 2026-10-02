# Petari Level Editor: interaction design

Status: design proposal (October 2026). Companion documents: [TECH.md](TECH.md)
(architecture, formats, feasibility) and [mockups/](mockups/index.html), a
clickable mockup of the main screens. A one-page summary of the big decisions is
in [SUMMARY.md](SUMMARY.md).

The editor is a mode of the game, not a separate program. You press **Tab**
while playing, the world stops where it is, and you are editing the galaxy you
were standing in. Press **Tab** again to play what you made. Everything in this
document follows from that.

Contents:

1. [What "great" means here](#1-what-great-means-here)
2. [Words the editor uses](#2-words-the-editor-uses)
3. [The core loop: Edit and Play](#3-the-core-loop-edit-and-play)
4. [Layout and look](#4-layout-and-look)
5. [Navigation](#5-navigation)
6. [Selection and transforms](#6-selection-and-transforms)
7. [The object browser](#7-the-object-browser)
8. [Properties and links](#8-properties-and-links)
9. [Gravity](#9-gravity)
10. [Paths](#10-paths)
11. [Cameras](#11-cameras)
12. [Missions (layers and scenarios)](#12-missions-layers-and-scenarios)
13. [Zones](#13-zones)
14. [A new galaxy](#14-a-new-galaxy)
15. [Terrain](#15-terrain)
16. [Safety: undo, autosave, checks, changes](#16-safety-undo-autosave-checks-changes)
17. [Saving and sharing](#17-saving-and-sharing)
18. [Controller](#18-controller)
19. [Keyboard and trackpad reference](#19-keyboard-and-trackpad-reference)
20. [Milestones and open questions](#20-milestones-and-open-questions)

---

## 1. What "great" means here

### Where today's tools hurt

Whitehole (and its forks, Whitehole Despaghettification and Whitehole Neo) is
the community's level editor for both Galaxy games. It made modding possible
and its object database is a decade of community knowledge. Our design starts
from its pain points. The research and sources are in
[research notes](#appendix-a-research-notes).

| Pain point (Whitehole and the usual workflow) | What we do instead |
| --- | --- |
| Testing an edit means saving, copying the archive, (re)starting Dolphin with Riivolution, booting, loading a file and walking or warping to the spot: minutes per iteration | **Tab** plays the edit where you are, in under two seconds. Tab again returns to the edit with the camera and selection unchanged |
| The 3D view is an approximation: no real lighting, effects, animation, or object state; many objects show as placeholder cubes | The viewport *is* the game: real models, lighting, effects, gravity and AI |
| Fields are shown raw: `Obj_arg0`..`Obj_arg7`, `SW_APPEAR`, `SW_A`, `CommonPath_ID`, `ShapeModelNo`, `l_id`. Meaning lives in a separate database, wiki or forum post, and is often missing | Every field has a human name, a widget that fits it (on/off, a list of choices, a number with units), and one line of documentation on hover. Unknown fields stay editable and say so honestly |
| Switches, paths, cameras and groups are linked by typing matching numbers into separate objects. Nothing shows the link | Links are drawn as wires in the 3D view. You make them by dragging from a socket to an object, and break them the same way |
| Gravity is invisible: you place planets and fields blind, then test and fall into space | A gravity view draws every field, colours the winner at each point, and shows the pull with flowing arrows. A probe under the cursor names the field that is pulling there |
| Layers (`common`, `layerA`..`layerP`) and the scenario masks that switch them per mission are raw data | A mission picker at the top: choose "Mission 2" and objects that don't appear in it fade out. Each object lists the missions it appears in as chips |
| Moving things on a round planet with world-axis gizmos (X/Y/Z) is a fight: objects end up floating or buried and tilted | Gravity is the default frame: drag an object and it slides over the ground, standing upright for the gravity where it is. Drop to ground (**End**) follows gravity, not world Y |
| Undo is missing or partial in older versions; crashes lose work; saving overwrites files in place | Every change is undoable, the edit is journalled to disk as you work (a crash loses nothing), and the disc is never written: edits live in a mod |
| Many tools for one job: Whitehole, Dolphin, Riivolution, a BCSV editor for text and scenario tables, a camera editor, an archive tool | One place. Names, missions, Level Select entries, cameras, and packaging are all in the editor |
| A Java app with a desktop-1990s UI, separate from the game it edits | A native part of the game, in the game's own look, with Mac conventions (Cmd shortcuts, trackpad gestures, file dialogs) |
| Saving while the game runs gives "Disc Not Read" unless the archive keeps its exact size | Edits are applied inside the running game; there's no file swap under the emulator |
| Silent crashes from configuration mistakes: an NPC with no text, a Launch Star with no path, more than 160 camera areas or 64 switch areas, a planet not registered in `PlanetMapDataTable` | Checks catch every known crash cause as you edit, explain it, and offer a fix; registrations happen automatically |
| The same field has several names across tools and tutorials ("l_id (aka: ID, Link ID)"); galaxies are listed by codename (`HeavensDoorGalaxy`) | One name per concept, in English, with the raw name shown once, in small type |
| One galaxy open at a time; copying a setup from another galaxy means retyping it | Copy and paste across galaxies; "See an example" pulls any of the game's own setups into yours |

### Principles

1. **The game is the canvas.** Edit what you see, in the light it will be
   played in. No separate "editor rendering" to learn or distrust.
2. **The edit-play loop is instant and lossless.** Switching costs a key and a
   second or two, and loses nothing: camera, selection, open panels, undo
   history. (Mario Maker's lesson: if testing is free, people test constantly,
   and levels get good.)
3. **Gravity is the default frame of reference.** "Up" in the editor is the
   way gravity pulls at that point, for the camera, the gizmos, snapping and
   placement. World axes are one keypress away, never the default on a planet.
4. **Speak human, keep the raw within reach.** Labels say "Coins in the ring"
   and "Appears when Switch 4 turns on"; an **Advanced** fold always shows the
   real field names and values for the people following a Whitehole tutorial.
   Nothing the file format can express is hidden from editing.
5. **Show the invisible.** Gravity fields, camera areas, switches, paths,
   death planes and mission membership are drawn in the scene when they're
   relevant: for the selection by default, everything on request.
6. **Direct manipulation first, precision always available.** Drag things
   where you want them; then type exact numbers, constrain to an axis, snap to
   a grid, or array along a path when you need to.
7. **Nothing is ever lost.** Undo everything, autosave everything, never touch
   the disc, show what changed against the original, and let any change be
   reverted on its own.
8. **It feels like Galaxy.** The game's fonts, the pause-screen teal, the gold
   of a focused button for the selection, the game's own sounds for
   placing and undoing. Without giving up density: data panels use a small,
   crisp UI type, and the Galaxy lettering is kept for titles, tabs and
   buttons.

### Borrowed on purpose

| From | We take |
| --- | --- |
| Super Mario Maker 2 | One button between Edit and Play; play starts where you are looking; Mario's trail from the last play-test is shown in the editor; placing by dragging from a palette; the game's sounds as feedback; undo you can't break |
| Blender | Modal grab with axis keys and typed values (`G`, `Z`, `200`, `Return`); a 3D cursor-like **play marker**; F3-style search for every command; snapping options in the header; scrubbable number fields that accept arithmetic |
| Unity and Godot | Scene tree (Outliner) plus Inspector; `F` to frame the selection; W/E/R for move/rotate/scale gizmos; a tinted viewport rim while in a special mode; prefab-like "smart parts" |
| Unreal | `End` drops the selection to the ground; fly with the right mouse button held and WASD |
| Figma | Selection model (click, Shift-click, marquee, double-click to enter a group or zone); mixed values shown as "Mixed" and set together; `Cmd+D` repeats the last duplicate's offset; a properties panel that changes with the selection; version history |
| Hammer and TrenchBroom | Brush blockout for terrain, grid sizes on `[` and `]`, texture lock, a clip tool |
| Dreams and LittleBigPlanet | Logic as visible wires between things, shown when you're working on logic and hidden when you're not; controller-friendly radial menus |
| The Home menu we already built | The footer band that names the keys that work right now; gold for focus; the panel style |

---

## 2. Words the editor uses

The editor uses the game's own concepts but names them for people. The raw name
is always one hover (or the Advanced fold) away.

| Editor word | Game data | Notes |
| --- | --- | --- |
| **Galaxy** | A stage (`EggStarGalaxy`) and its scenario archive | Named in English ("Good Egg Galaxy") from the game's messages |
| **Zone** | A zone archive placed in a galaxy (`SoramameZone`, `stageobjinfo`) | Kept as a concept: zones move as a unit, load separately, and two are shared between galaxies. Shown with a readable name where we have one ("Bean planets") and the file name in small text |
| **Mission** | A scenario (`ScenarioData`) | "Mission 1: Dino Piranha". Comet and hidden missions labelled C and H as in Level Select |
| **Object** | A placement row (`objinfo`, `mapparts`, `planetobjinfo`, `areaobjinfo`, `cameracubeinfo`, `demoobjinfo`, `startinfo`, `generalposinfo`, `soundinfo`, `childobjinfo`) | Everything placeable is an object; categories tell them apart |
| **Gravity** | `planetobjinfo` rows (`GlobalPointGravity`, `GlobalDiskGravity`, ...) | Drawn as shapes, edited with handles |
| **Area** | `areaobjinfo` rows (`SwitchCube`, `DeathCube`, `RestartCube`, `LightCtrlCube`, `ChangeBgmCube`...) | Invisible volumes that do things |
| **Camera area** | `cameracubeinfo` rows plus their camera in the galaxy's camera file | "Camera area" + "Camera" (the shot it gives) |
| **Path** | `commonpathinfo` + points | Drawn as a curve with handles |
| **Switch** | The switch numbers in `SW_*` fields | A switch is a numbered signal: something turns it on, others react. Users can name a switch ("Gate open"); names are saved with the editor project, not the game files |
| **Start** | `startinfo` rows | Where Mario appears for a mission or a warp |
| **Appears in** | Layer membership (`common`, `layerA`..`layerP`) through the scenario masks | Shown as mission chips, never as layer letters except under Advanced |
| **Play marker** | (editor only) | Where Play puts Mario. Drawn as a translucent Mario |

---

## 3. The core loop: Edit and Play

### Two states, one key

```
          Tab                                   Tab
 Play ───────────► Edit (frozen frame) ─────────────► Play (from the play marker)
   ▲                     │        ▲                        │
   │      Shift+Tab      │        └────────── Tab ─────────┘
   └──── Resume as-is ───┘          (freezes this frame; the trail of the run is drawn)
```

- **Entering Edit never changes what's on screen.** Tab (or F1 → Level
  Editor, or the controller's hold-Minus) freezes the game exactly as photo mode
  does: the frame stays, the HUD fades out, the editor camera starts at the
  game camera, panels slide in from the sides (150 ms), and a thin gold rim and
  an **EDITING** chip mark the viewport. Game audio pauses; the galaxy's music
  continues at low volume (a setting).
- **Playing always starts from your layout.** **Tab** in Edit resets the world to
  the edited layout (enemies at home, coins back, switches off) and drops
  Mario at the **play marker**. Resetting is a fast reload of the stage from
  your layout (target about 2 s; 3.9 s measured before the reload path is
  trimmed, TECH.md); it plays behind the game's
  own star-shaped screen wipe, so it reads as "the level starts" rather than a
  stall. When nothing has changed since the last reset, Play is instant.
- **Resume** (**Shift+Tab**) continues the frozen moment instead, with no reset.
  It's offered only when nothing that needs a reset was changed (a pure camera
  fly-around, or edits the game applied to live objects).
- **Play from the mission start**: hold **Tab** for half a second (Mario Maker's
  hold-to-play-from-the-start), `Cmd+Shift+Tab`, or the Play button's menu.
- **Returning** (Tab during a play-test) freezes that frame again. The camera
  goes to where you were editing (not where Mario is), the selection and panels
  are as you left them, and Mario's **trail** from the run is drawn as a dotted
  line with jump arcs, so you can see where a jump fell short. **Edit here**
  (the button on the toast, or `Cmd+Return`) moves the camera to Mario instead.
  Returning is instant: the world isn't reset until you play again (or press
  Reload), so the moment of failure stays on screen to study, and **Keep** (below)
  can copy where things ended up into the layout.
- After a play-test, the frozen world is in its **played state** until the next
  reset, and a chip under the EDITING label says so ("Played state · switches
  13, 30 on · Reload to reset"). It matters in three places: switches stay as
  played (an object you link to switch 13 appears at once if 13 is already on);
  collected and defeated objects are gone (their home ghosts stay selectable);
  and Mario's health, power-up, Star Bits and timers are as played. Play always
  resets all of it.

Why this rule: in Mario Maker the course you edit is always the course as
built. Galaxy's world has state (a defeated enemy, a collected star bit, a
switch on), and showing a half-played world as if it were the layout is
confusing. But jumping the world back the instant you press Tab would break the
"keep the frame" promise. So on entering Edit the live frame stays, and objects
whose live state differs from the layout are marked:

- An object away from its home (a walking Goomba, a moved platform) is drawn
  live where it is, with a faint **home ghost** at its placement and a dotted
  line between them. Selecting it selects the placement: the gizmo sits on the
  home ghost.
- An object that is gone (a defeated enemy, collected coins, a broken crystal)
  shows only its home ghost, so it's still selectable.
- **Reload** (`Cmd+R`, the toolbar's circular arrow) rebuilds the frozen world
  from the layout without playing: enemies home, coins back, and any pending
  changes applied (below). The camera, selection and panels stay put.
- **Keep** (`K`, Unreal's "keep simulation changes"): with live objects
  selected, copies where they are *now* into the layout. Play until a rolling
  rock or a pushed block settles where you like it, Tab, select it, K. Not
  offered for objects that ride a path, are parts of another object, or work
  out their own place each time they start (the catalog knows which); K is
  greyed for them with the reason in the footer. The first edit that needs a reset
  shows a hint: "The world resets to your layout when you play."

### Live changes and pending changes

Most edits are live: the game redraws every frame while frozen, so a moved
object moves as you drag, and a changed setting re-creates that one object
from your layout in well under a millisecond (TECH.md, measured). Adding an
object is instant, even when its model wasn't loaded in the stage (loading one
takes under 2 ms, measured); only big planets show a short "loading model"
state.
Gravity shapes and settings are live too.

A few kinds of change only take effect when the stage is rebuilt: zones (adding
or moving one), camera settings, which missions an object appears in, start
points and lighting. These show as **pending**: the object is drawn as a blue
ghost where it will be, its row in the Outliner has a blue dot, and the
toolbar's Reload button reads **Reload · 3 pending**. Play reloads anyway, so
pending changes never need a separate step; Reload (`Cmd+R`) applies them
without playing.

While editing, the game's distance culling is off, so everything in the stage
is drawn and selectable, including objects the game had hidden from its own
camera.

### The play marker

A translucent Mario standing in the scene, with a small arrow for facing.

- It starts where Mario stood when you entered Edit.
- Drag it like any object (it sticks to the ground and stands up with gravity).
  **M** drops it on the ground under the cursor, facing away from the camera.
- **Tab** plays from it. The Play button's menu: *Play from marker* (Tab),
  *Resume* (Shift+Tab), *Play from mission start* (Cmd+Shift+Tab), *Play from
  start point...* (lists this mission's starts and warps).
- Play-test options in the same menu: *Invincible* (no damage), *Infinite
  life*, *Show trail*, *Test as Luigi*. They apply to play-tests only.

### Play-tests don't touch your save

A play-test runs on a scratch copy of the current save file: stars, coins and
unlocks from a test are thrown away. Collecting a Power Star in a play-test
plays the get-star sequence, then returns to Edit instead of leaving the galaxy
(with a toast: "Star collected in 0:42"). Dying returns to Edit with the
frame frozen at the moment of death and the trail drawn.

### Feedback while playing

A small, unobtrusive chip at the top centre ("Play-test · Tab to edit") so a
screenshot or a moment's confusion never mistakes a test for the real game.
Nothing else changes: the HUD is the game's.

---

## 4. Layout and look

### The window

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ◀ Good Egg Galaxy ▸ Bean planets   [Mission: All ▾]  ↖ ✥ ⟲ ⤢  ⊞ ◎ ⌁ │ ↶ ↷ │ ⚠ 3 │ ⇪ │ ▶ Play │  toolbar
├───────────┬───────────────────────────────────────────────┬──────────────────┤
│ Outliner  │                                               │ Inspector        │
│ Browser   │                                               │  (the selection) │
│ (tabs)    │              the game (viewport)              │                  │
│           │                                               │                  │
│           │                                               │                  │
│           │                                               ├──────────────────┤
│           │                                               │ Help for the     │
│           │                                               │ hovered field    │
├───────────┴───────────────────────────────────────────────┴──────────────────┤
│ ⓐ Click: select  ⓑ Drag: move on ground  G: grab  End: drop to ground ... │ Memory 64% │  footer
└──────────────────────────────────────────────────────────────────────────────┘
```

- **Viewport**: the game, rendered into the space between the panels at that
  space's aspect ratio (not letterboxed, if the renderer allows; see TECH.md).
- **Left dock**: tabs for **Outliner** (everything in the galaxy, by zone and
  category), **Browser** (objects to add), **Checks** and **Changes**. One open
  at a time; the tab strip is the Galaxy-lettered header.
- **Right dock**: the **Inspector** for the selection, with a help strip at its
  foot that explains whatever field the pointer is over.
- **Toolbar**: where you are (galaxy ▸ zone breadcrumb), the mission picker,
  tools, snapping, undo/redo, the Checks badge, Share, and the gold **Play**
  capsule.
- **Footer band**: the Home menu's convention. It names the keys that do
  something right now, with icons, and changes with the tool and the hover.
  The right end shows the memory meter (section 16).

Sizes on a Mac:

| Window | Left | Right | Viewport |
| --- | --- | --- | --- |
| 1440 × 900 (MacBook Air default) | 260 | 300 | 880 × 790 |
| 1728 × 1117 (MacBook Pro 16") | 280 | 320 | 1128 × 1005 |
| 2560 × 1440 (external display) | 300 | 360 | 1900 × 1330 |

- **Focus mode** (`Cmd+.`, or a double-click on the viewport's empty sky)
  hides both docks; the toolbar and footer shrink to translucent bands over the
  image, like the pause screen. Docks then appear as overlays when you hover
  the window's left or right edge, or press their key (`B` browser, `I`
  inspector), and hide again when you move away.
- Docks are resizable and collapsible to icon rails (40 px). Widths are
  remembered.
- Fullscreen (the green button, `Cmd+Ctrl+F`) uses the same layout. The
  editor never opens its own windows: dialogs are sheets in the game window
  (the Mac convention, and the way the Home menu works).

### The look

The editor wears the Home menu's style (native/HOME_MENU.md), tuned for long
sessions and dense data:

| Element | Style |
| --- | --- |
| Panels | The Home menu panel: a teal gradient (`#085773` → `#03243D`), a 1 px cyan rim (`#66DBFF` at 60%), a faint white inner line. 92% opaque over the game so a hint of the scene shows through at the edges |
| Section headers | The table header band: translucent cyan (`#59CCFF` at 22%) with light cyan text (`#99EBFF`) |
| Titles, tabs, buttons | The game's lettering: MessageFont26 for tabs, buttons and panel titles; MenuFont64 only for dialog titles ("New Galaxy", "Share") |
| Data (names, numbers, fields) | A small, crisp UI sans with tabular figures: 12–13 px. The Galaxy font is a bitmap font made for 26 px and gets soft and wide when small; dense data needs a type designed for it. See open question Q2 |
| Buttons | The game's white-to-blue capsules with a cyan rim; the primary action (Play, Save, Share) is the gold focused capsule |
| **Selection** | **Gold** (`#FACC38`): the focused-button colour. A gold outline around the selected model, a gold dot in the Outliner, a gold gizmo centre |
| Hover | Cyan (`#66DBFF`) outline |
| Changed from the disc | A gold dot beside the field (the remapper's "changed" convention); reset by clicking it |
| Warnings / errors | Orange (`#FF8C40`, the remapper's conflict colour) / red (`#FF5A5A`) |
| Icons | The game's PictureFont button icons in the footer, and a small set of line icons for tools (drawn by us; no disc art in the repo) |
| Sounds | The game's own menu sounds for select, place, delete, undo and errors (cursor move, "decide", cancel). Placing an object plays that object's own appear sound when it has one (a Coin chimes). One setting turns them off |

Readability rules:

- Text never sits directly on the game image. It's always on a panel or band
  at ≥ 85% opacity.
- Minimum 12 px text at 1x, 11 px only for secondary labels (raw names, units).
- Sizes follow the window (as the Home menu follows the image height) with a
  **UI scale** setting (90%–150%).
- Everything is reachable by keyboard; focus is always visible (gold).

---

## 5. Navigation

The editor camera grows out of photo mode's free camera (which already flies,
looks and keeps the horizon on planets).

### Orbit and fly, without choosing

The camera always has a **pivot**: the point under the cursor when you start
orbiting (or the selection's centre if the cursor is over sky). A small cyan dot
shows it while you orbit.

| Do | Trackpad | Mouse | Keyboard |
| --- | --- | --- | --- |
| Orbit around the pivot | two-finger swipe | middle-drag, or Option+drag | J L I K |
| Pan | Shift + two-finger swipe | Shift+middle-drag | |
| Zoom toward the cursor | pinch | wheel | Z / X |
| Look around (fly) | | hold right button and move | |
| Fly | | hold right button + W A S D, Q down, E up | (W A S D alone fly when no object is selected and the option is on) |
| Fly faster / slower | | while flying, scroll up / down; hold Shift for 4x | |
| Frame the selection | | | **F** (twice: frame it closer) |
| Frame everything / the zone | | | **Shift+F** |
| Back / forward through views | three-finger swipe left / right | mouse back/forward buttons | Cmd+[ / Cmd+] |

- Orbit is the default because you are usually working *on* something; flying
  needs the right button held, as in Unity and Unreal, so W/A/S/D/Q/E stay free
  for tools when you aren't flying.
- Zoom is proportional to distance from the surface under the cursor, so a
  pinch moves you a metre near a coin and a planet's width out in space. Zoom
  never passes through the surface under the cursor: it slows and stops.
- Speed adapts the same way and shows briefly as a toast ("Fly speed 3x").
- Trackpad scroll means orbit everywhere in Petari (the Odyssey camera does the
  same), so muscle memory carries over from play.

### Gravity-aware up

On a planet, "up" is away from the planet, not world +Y. The editor camera's
up is the **gravity at the pivot** (sampled from the galaxy's real fields).

- Orbiting turns around that up, so the horizon of the planet you're looking at
  stays level.
- When the pivot moves to another planet (a new orbit point, or **F** on an
  object there) up blends to the new gravity over 0.4 s, like the game's own
  camera when Mario crosses between fields.
- Where there's no gravity (deep space), up stays as it was.
- **Up: Gravity / World** toggle on the viewport's corner widget (and `U`).
  World is for laying out the galaxy as a whole.
- The corner widget is a small **gravity compass**: a sphere with a gold arrow
  for local down and grey world axes. Clicking an axis looks along it (top,
  front, side views, gravity-relative), as Blender's navigation gizmo does.

### Seeing the whole galaxy

Zoom far enough out, or press **Shift+F** with nothing selected, and the view
becomes a galaxy overview: zones are labelled with their names floating at
their centres (Galaxy-lettered, like the game's galaxy map), and clicking a
label flies to that zone. This is how you move between planets that are tens of
thousands of units apart.

### Bookmarks

**Views** in the toolbar's view menu: save the current view with a name;
`Option+1`…`Option+9` recall the first nine. Saved in the editor project.

---

## 6. Selection and transforms

### Selecting

| Do | Effect |
| --- | --- |
| Click | Select the object under the cursor (the outline shows what a click would take before you click) |
| Shift-click | Add to the selection |
| Cmd-click | Toggle in the selection |
| Drag on empty space or sky | Marquee: everything whose centre is inside (Option: everything touching) |
| Double-click | Select all of the same kind in the zone ("all Coins here"); on a zone's planet: enter the zone (section 13) |
| Click again on an overlapping spot | Cycle through what's under the cursor, front to back (with a small list popover after the second click) |
| Esc | Deselect, then (pressed again) leave the zone |
| Cmd+A | Select all in the current zone and mission filter |
| Tab-like cycling | `[`/`]` are grid sizes, so cycling uses **Option+↑/↓** (next/previous of the same kind) |

Invisible things (areas, gravity, cameras, starts, paths) are selectable only
when their kind is shown (see Visibility below) and are picked by their outline,
not their volume, so a huge gravity sphere doesn't swallow every click.

**Visibility** (the eye menu in the toolbar, and shortcuts): Gravity (`1`),
Areas (`2`), Camera areas (`3`), Paths (`4`), Links (`5`), Starts (`6`),
Hidden-in-this-mission (`7`). Each kind is Off, Selection only (default: a
selected object's own gravity, path and links are shown), or All.

### Moving on a planet

The default way to move something is to **drag it**. It slides over the ground
under the cursor, staying upright for the gravity at each point, and keeps its
height above the ground (a coin floating 100 units up stays 100 up). This is
Mario Maker's directness made spherical.

- **Option-drag** moves in the view plane instead (no ground following).
- **Option+Shift-drag** duplicates and moves (Figma's Option-drag).
- Dragging over a gap keeps the last height and shows a red "no ground" ring;
  releasing there leaves it floating (it's allowed) with a Check warning if
  it's outside every gravity field.

### The gizmo

For precise moves, **W** (move), **E** (rotate), **R** (scale) show a gizmo, and
**Q** returns to plain select.

- The gizmo's frame, chosen on the toolbar (and `,` to cycle):
  **Gravity** (default; up = away from gravity at the object, forward = the
  object's facing flattened onto the ground), **Local**, **World**, **View**.
- The move gizmo has arrows, planar handles and a centre "slide on surface"
  handle. Rotate has rings; the up ring (turn to face) is gold and thickest
  because it's the one you want most. Scale has a uniform handle in the centre.
- On a planet, moving along the gravity forward axis is a *curve* along the
  surface, not a straight line into space, when **Surface** snapping is on.

### Quick transforms (Blender style)

Hover the viewport and press:

- **G**: grab: the selection follows the cursor (on the surface if Surface snap
  is on). Then **X / Y / Z** constrain to the gizmo frame's axes (in Gravity
  frame: X side, Y up, Z forward); a second press switches to world axes.
  Type a number to move exactly that far (`G` `Y` `200` `Return` lifts it 200
  units against gravity). **Return** or click confirms; **Esc** or right-click
  cancels.
- **Shift+R** rotate and **Shift+S** scale work the same way (R and S alone
  are taken by the gizmo modes and by flying).
- While any drag or quick transform runs, the footer shows the axis, the
  typed value and the keys that apply.

### Snapping

Snapping options sit in the toolbar, with their keys:

| Snap | Default | Key | Behaviour |
| --- | --- | --- | --- |
| Surface | On | `Cmd+1` | Dragging follows the ground; new objects land on the ground |
| Align to gravity | On | `Cmd+2` | Objects stand up for the gravity where they are, as they move |
| Grid | Off | `Cmd+3`; sizes with `[` `]` | Grid on the gravity tangent plane at the object (50, 100, 200, 500, 1000 units). A faint grid patch appears around the object while moving |
| Angle | 15° | `Cmd+4` | Rotation steps |
| Objects | On | `Cmd+5` | Edges and centres of nearby objects of the same kind line up (Figma's smart guides, in 3D: a cyan guide line appears when two Coins line up) |

Hold **Cmd** while dragging to suspend all snapping for that drag.

### Gravity commands

| Command | Key | Effect |
| --- | --- | --- |
| Drop to ground | **End** | Moves each selected object along its gravity until it touches the ground. Each object uses its own gravity: a selection spread over a sphere lands all round it |
| Stand up | **Shift+End** | Rotates each object so its up matches the gravity where it is, keeping its heading |
| Drop and stand | **Cmd+End** | Both |
| Lift | **Page Up / Page Down** | ±1 grid step along gravity |

### Multi-select

- The Inspector edits all selected objects together; differing values read
  **Mixed** (Figma) and typing sets all of them.
- Transforms pivot on the selection's centre by default; **Individual** (the
  toolbar pivot menu, `.`) rotates and scales each around its own origin and
  gravity, which is what you want for "turn all these Goombas to face the
  centre of the planet".
- **Face** (`Cmd+L`): turn the selection to face the play marker, the camera, or
  another object (pick it).

### Duplicate, array, along a path

- **Cmd+D**: duplicate, offset one grid step along the gravity forward, and
  selected. **Cmd+D again** repeats the *last* offset and turn (Figma's repeat
  duplicate): move the first copy where you want it, then Cmd+D, Cmd+D... lays
  a line or curve of coins along the planet.
- **Array** (`Cmd+Shift+D`, or the Inspector's Arrange section): pattern the
  selection with live preview and on-canvas handles:
  - **Line**: count or spacing; drag the end handle and the count updates.
  - **Ring**: around a centre (the play marker, the pivot, or a picked object),
    lying on the surface; radius handle; "face outward / inward / along".
  - **Grid**: rows × columns on the surface.
  - **Along a path**: pick an existing path or draw one (section 10); count or
    spacing; "align to path".
  - **Along Mario's trail**: from the last play-test, drag over a stretch of
    the dotted trail to select it, and fill it with the selection at a spacing.
    This is the fastest way to put a line of coins along a real jump arc.
  An array stays editable (count, spacing, radius) until you click **Apply**,
  or until you select something else; then the copies are ordinary objects.
- **Paint** (hold **P** while dragging from the Browser, or the Paint
  tool): stamp copies along the drag at a spacing, on the surface.

### Delete, hide, lock

- **Delete** / **Backspace**: delete (undoable). Objects that others link to
  ask first: "3 objects use Switch 4 from this Goomba. Delete anyway?"
- **H** hides the selection in the editor (not in the game); **Option+H** shows
  everything again. **Cmd+Shift+L** locks it against selection (huge planets
  you keep clicking by accident). Both are editor-only and saved in the project.

---

## 7. The object browser

Opened from the left dock's **Browser** tab (`B`), or as a quick search at the
cursor with **Space**.

### The panel

```
┌ Browser ─────────────────────────────┐
│ 🔍 goomba                            │
│ [In this galaxy] [All] [★] [Recent]  │
│ Enemies · Collectibles · Platforms … │
│ ┌────┐ ┌────┐ ┌────┐                 │
│ │ 🖼 │ │ 🖼 │ │ 🖼 │                 │
│ │Goom│ │Para│ │Mini│                 │
│ └────┘ └────┘ └────┘                 │
│ Goomba (Kuribo)                ★     │
│ Walks toward Mario; defeated by a    │
│ spin or a stomp.                     │
│ Needs: nothing · Used 214× in 23 gal.│
│ [See an example]                     │
└──────────────────────────────────────┘
```

- **Thumbnails** are rendered by the game from its own models, at runtime, the
  first time the browser needs them, and cached in the user folder
  (`editor/thumbs/`). Nothing is shipped. The first time the editor opens it
  renders them all in the background (about 1,200 objects; a small progress
  ring on the Browser tab), drawing the visible cards first. Until a thumbnail
  is ready, the card shows the category icon and the name; objects made of
  several parts keep a category icon. Hovering a card turns the
  model slowly (a 2-second turntable).
- **Scopes**: *In this galaxy* (models already loaded: they place instantly),
  *All*, *Favourites* (★ on a card), *Recent*. Objects that aren't loaded yet
  show a small cloud badge: placing them may take a moment and adds their
  archive to the galaxy's load list (TECH.md).
- **Categories**: Enemies, Bosses, Collectibles (coins, Star Bits, 1-Ups, Power
  Stars), Launchers (Launch Stars, Sling Stars, cannons, springs), Platforms
  and moving parts, Planets and terrain, Blocks and breakables, Power-ups, NPCs
  and signs, Gravity, Areas, Cameras, Paths, Starts and warps, Lights and
  sound, Effects and decoration, Logic (switch helpers, timers, counters).
- **Search** matches the English name, the internal name, the Japanese-derived
  names the community uses (Kuribo, Karikari, Takobo), and tags ("fire",
  "water", "boss"). Results are ranked: in this galaxy first.
- **Card details**: English name and internal name; one sentence of what it
  does; **Needs** chips (a path, a switch, a message, a camera); how often the
  game uses it; ★.
- **See an example**: lists the game's own placements of this object across
  every galaxy ("Good Egg · Bean planets · 6×"). Clicking one opens a preview of
  its settings, and **Use these settings** places a new object with exactly
  that setup (args, scale, the kind of links it needs, left for you to
  connect). This replaces the community habit of copying a working object from
  another level by hand.

### Placing

- **Drag a card into the viewport**: the object follows the cursor on the
  ground, upright for gravity, with a ghost preview, and lands where you
  release. Esc while dragging cancels.
- **Click a card** arms it: each click in the viewport places one (Mario
  Maker's stamp); Esc or Q disarms. The footer says so.
- **Space** at the cursor: a search box opens under the pointer; Return places
  the first result where the cursor was.
- **Smart parts**: placing a planet also places the gravity the game normally
  gives that planet (taken from the game's own placements of it), already
  fitted, unless you turn that off in the card. Likewise a Launch Star offers to
  draw its flight path, and a Sling Star its target.

---

## 8. Properties and links

### The Inspector

```
┌ Inspector ───────────────────────────┐
│ [thumb] Octoomba            ★  ⋯     │
│ Takobo · zone Bean planets · #66     │
├ Transform ───────────────────────────┤
│ Position   x -2914  y -12961 z -15332│
│ ↳ above ground  0      heading 135°  │
│ Rotation   0 135 0     Scale 1.00 ⛓  │
├ Behaviour ───────────────────────────┤
│ Walks in a     Circle ▾          •   │
│ Turn speed     4                     │
│ Patrol radius  200 u ━━━○━━          │
│ Spit rocks     ✓                     │
├ Appears in ──────────────────────────┤
│ (All) 1 2 3 4C 5C 6H                 │
├ Links ───────────────────────────────┤
│ Appears when   ○ Switch 4 "Gate"  ⨯  │
│ When defeated  ◉ turns on Switch 7 ⨯ │
│ Follows path   ○ none        [Draw]  │
├ Advanced ▸ ──────────────────────────┤
└──────────────────────────────────────┘
│ Patrol radius: how far Octoomba walks│
│ from its home before turning back.   │
│ Raw: Obj_arg3 (default 200).         │
└──────────────────────────────────────┘
```

- **Header**: thumbnail, name (double-click to give *this* object a label like
  "Gatekeeper Goomba": editor-only, shown in the Outliner), internal name, zone,
  and id. The ⋯ menu: copy/paste settings, reset to the disc's values, find
  similar, see examples.
- **Transform**: position, rotation (degrees), scale with a uniform lock. Below
  position, the **gravity-relative** readout: height above the ground and
  heading on the surface, both editable. Number fields are scrubbable (drag
  the label), accept arithmetic (`+100`, `*2`, `/4`) and units, and take
  Up/Down arrow steps (Shift for 10x).
- **Behaviour**: the object's arguments with human names and fitting widgets:
  - a list of named choices for modes ("Walks in a: Line / Circle / Stays put");
  - on/off for flags;
  - numbers with units and sensible slider ranges (units, frames shown as
    seconds, degrees);
  - "Default" in faded text for a `-1` value, with the default it stands for
    when known ("Default (200)");
  - a picture picker for `ShapeModelNo` variants;
  - colour swatches where an argument picks a colour.
- Fields changed from the disc's value have a **gold dot**; click it to revert
  that field. New objects have no dots.
- **Appears in**: mission chips (section 12).
- **Links**: switches, path, camera, message, group (below).
- **Advanced** (folded): every field of the row with its real name
  (`Obj_arg3`, `SW_APPEAR`, `ClippingGroupId`, `l_id`...), editable as raw
  values. Following a Whitehole tutorial always works here.
- **Help strip**: one or two sentences for the field under the pointer, its raw
  name and default, and, for undocumented fields, the truth: "Obj_arg5 isn't
  documented yet. The game reads it as a whole number (Takobo::init)."
  plus **Suggest a name**, which opens the object catalog entry (TECH.md) for
  contributors.

Where the names come from: an open object catalog in the repo, written from
the decompiled source (every `getJMapInfoArgN` call names what it reads, and
every switch registration says whether the object reads or writes it) and
from community knowledge where its licence allows. Which links an object
*needs* is proven, not guessed: a test spawns every one of the game's ~1,180
object kinds with no links and records which crash (TECH.md). The catalog is data, not
code, so improving a label never needs a build. (Open question Q4.)

### Links, drawn as wires

Galaxy wires things together with numbers: a defeated enemy turns on switch 7
(`SW_DEAD = 7`); a Star Bit group appears when switch 7 is on
(`SW_APPEAR = 7`). The editor draws those numbers as **wires** in the 3D view.

- **Sockets**: each link field in the Inspector has a socket: ◉ out (this
  object *turns on* the switch: defeat, collect, area entered) or ○ in (this
  object *reacts* to the switch: appears, wakes, activates, dies).
- **Wires** run from senders to receivers, in a colour per switch number, with
  slow flowing dashes showing direction. A label at the wire's middle names the
  switch ("Switch 7 · Bridge appears"). Wires bend along the planet's surface
  rather than cutting through it, so they read on a sphere.
- **Make a link**: drag from a socket in the Inspector, or from the selected
  object's socket handle in the viewport (a small ring beside the gizmo), onto
  another object. On release, a popover asks how it should react ("Appear /
  Wake up / Activate / Die when switch is on"), offering only the switch fields
  that object reads. Which side an object can be on comes from the game itself
  (each class registers its switches as read or write: `useStageSwitchWriteDead`,
  `useStageSwitchReadAppear`...).
- **Switch numbers are allocated automatically**: the editor never makes you
  choose a free number. The game has two kinds: 0–127 belong to one zone,
  1000–1127 to the whole galaxy. A link within one zone gets a zone number; a
  link that crosses zones gets a galaxy number, and the wire label says
  "galaxy-wide". Running out is a Check.
- **Pick** (the eyedropper ⊙ beside a field): click the object to link to.
- **Break a link**: ⨯ in the field, or select a wire and Delete.
- **Name a switch**: double-click its label. Names are kept in the editor project.
- **When wires show**: for the selection (default), for everything (`5`), or
  none. With everything on, unselected wires are thin and dim (Dreams' rule:
  logic is visible when you're working on logic).
- **Switches panel** (in the Outliner tab, "Switches"): every switch in the
  galaxy, its name, senders and receivers; clicking selects them all and frames
  them. A switch with receivers but no sender is a Check warning.

The same wire treatment covers the other links:

| Link | Drawn as | Made by |
| --- | --- | --- |
| Path (`CommonPath_ID`) | A thin line from the object to the nearest point of its path; the path itself highlighted | Socket drag onto a path, or **Draw** a new path from the field |
| Camera (`CameraSetId`, camera areas) | A line to the camera's eye icon | Socket drag onto a camera area or camera |
| Group (`GroupId`, `ClippingGroupId`, `ViewGroupId`) | A hull around the group's members, tinted | The Group field's pick, or Cmd+G with a dialog for which kind of group |
| Message (`MessageId`) | A speech-bubble icon above the NPC or sign; the text inline in the Inspector, editable | Inspector |
| Parent / child (`ParentID`, `childobjinfo`) | A dashed line to the parent | Socket drag |

---

## 9. Gravity

Gravity is what makes Galaxy Galaxy, and today it's the most blind part of
editing. The **Gravity view** (`1`, or the toolbar's gravity tool) makes it
visible.

### Seeing fields

- Every field is drawn as its shape (sphere, disk, cylinder, cube, cone,
  plane-in-box, segment, wire) in a translucent tint per type, with its
  boundary as a soft edge, depth-tested against the world so you see where it
  meets the ground.
- **Flow arrows**: small particles drift along the pull, in the field's colour,
  so you see "down" everywhere at a glance; the strongest (winning) field at
  each point decides the colour.
- **Reach**: a field pulls anything closer than its **Range** plus its
  **Distant** (Range `-1`: everywhere). The reach is drawn as a dashed shell.
  Distant also makes the field pull as if it were that much closer, which
  matters where equal-priority fields blend (below).
- **Gravity probe**: the cursor carries a small arrow that points where gravity
  pulls at the surface point under it, with a label: "Disk gravity #6 ·
  priority 10 · beats 2 others". Hold **Option** to probe in mid-air at the
  cursor's depth.
- **Overlaps**: where two or more fields overlap, the losing ones are drawn
  hatched. Clicking a hatched region lists the fields there in priority order.
- **Dead zones**: points on walkable ground that no field covers are marked in
  red. This finds the classic "falls into space" bug before a play-test.

### Editing fields

- Select a field (in the Gravity view its outline is clickable) and it gets
  shape handles: radius for spheres and disks, height and radius for
  cylinders, face handles for boxes, the axis for segments. Handles snap to
  the selected planet's bounds when you drag near them.
- **Fit to planet** (Inspector): sizes and places a field to wrap the planet it's
  linked to or the one under it.
- **Inverse** (pull outward, for the inside of a hollow planet) is a toggle
  with an arrow preview.
- **Priority** is a ranked list, not a number: the Gravity panel (Outliner tab
  "Gravity") lists the fields at the probe or in the zone, highest priority
  first. In the game the highest priority wins outright, and fields of *equal*
  priority add their pulls together (the nearer, stronger one dominates). The
  list shows that: fields sharing a priority sit on one row marked **blend**.
  Drag a field between rows to rank it, onto a row to blend it; the editor
  writes the numbers.
- Adding a field: drag it from the Browser's Gravity category onto a planet;
  it fits itself (smart default) and you adjust.
- **Test fall**: `T` in the Gravity view drops a ball from the cursor that
  follows the real gravity with a trail, the cheapest possible play-test of a
  field. Several balls stay for comparison until you press `T` with Option.

---

## 10. Paths

Paths (rails) drive moving platforms, patrolling enemies, Star Bit trails, and
camera moves.

- Shown (selection only, by default) as a smooth curve with direction chevrons,
  its points as dots, and Bézier handles on the selected point. Closed loops
  are drawn as loops.
- **Pen** tool (`P` with nothing to paint): click on surfaces to lay points at
  a chosen height above the ground (the Pen's height field, default 100);
  drag while clicking to pull handles; click the first point to close; Return
  finishes. Points snap to the surface and to gravity as objects do.
- On a path: click to select a point, drag to move, Option-click on the curve
  to insert a point, Delete to remove. Points have their own fields in the
  Inspector (speed, wait time, the per-point arguments with names).
- **Used by**: the path's Inspector lists every object and camera that follows
  it, with wires; **Preview** plays a ghost of each user along the path at its
  speed, looping, in the frozen world. Scrub the timeline under the preview.
- **Make a path from**: Mario's trail (select a stretch of the trail), or a
  selection of objects (a path through them in order).

---

## 11. Cameras

Galaxy's cameras are set by camera areas (volumes); entering one switches to its
camera (a fixed shot, a rail, a follow camera with set angles...).

- In the **Camera view** (`3`): camera areas are drawn as volumes with an eye
  icon, coloured by camera type, with the priority number on overlap.
- Selecting a camera area shows a **picture-in-picture preview** in the
  viewport's corner: what the game's camera would show with Mario standing at
  the play marker (or at the area's centre if the marker is outside). Drag the
  marker around inside the area and the preview follows. If the renderer can't
  afford a second view, the preview falls back to a still taken when the area is
  selected (TECH.md).
- **Look through** (`0`, the Blender convention): the main viewport becomes that
  camera; editing the camera's fields updates it live. Press 0 again to go
  back.
- **Make from view**: fly the editor camera to a shot you like and press
  **Set camera from view**: the camera's position and angles are taken from the
  editor camera (for fixed cameras), the fastest way to author a shot.
- The camera's fields (type, angles, distance, rail, flags) have names and
  help like any object.

---

## 12. Missions (layers and scenarios)

A galaxy's missions share one world; each mission turns some groups of objects
(layers) on and off. Players think in missions, so the editor does.

- **Mission picker** in the toolbar: *All* or one mission ("1 · Dino Piranha").
  The game only builds the objects of one mission at a time, so the world you
  edit is always one mission's (the one you were playing when you entered).
  Picking another mission reloads into it (2–4 s, behind the wipe, skipping
  the mission's intro).
  Objects of other missions are drawn from your layout as **ghosts**: their
  model without behaviour, translucent, or a labelled box when their model
  isn't loaded. They're selectable and editable like any object. Picking a
  mission:
  - fades out objects that don't appear in it (20% opacity, not selectable
    unless Visibility → Hidden-in-this-mission is on);
  - makes Play start that mission (and resets to its layout);
  - makes new objects appear **only in this mission** by default; the toast
    after placing says so with an **All missions** button to change it.
- **Appears in** chips on every object: `All` or any combination of `1 2 3 4C
  5C 6H`. Toggle chips to change membership. Behind the scenes the editor moves
  the object to the layer whose mission set matches, or creates one (TECH.md);
  layers are never named in the main UI.
- **Missions panel** (Outliner tab "Missions"): each mission's name (editable,
  in every language the game has, with the current language first), its star
  (which Power Star, where: a link to the star object), its start point, the
  mission type (normal, comet: speed run / purple coins / daredevil / cosmic
  race / fast foes; hidden), and **Add mission**.
- Advanced users can still see the raw layers: the Outliner's ⋯ menu → *Group
  by layer*, and each object's Advanced fold.

---

## 13. Zones

A galaxy is made of zones: each zone is a separate archive with its own objects,
placed in the galaxy at a position and turn. Most edits don't need to care, but
three things do.

- **The Outliner** groups objects by zone, then by category. The zone the
  pivot is in is highlighted. Every zone has a readable name (from our table,
  else the file name split into words).
- **Entering a zone** (double-click its planet, or its row): the rest of the
  galaxy dims, the breadcrumb shows "Good Egg ▸ Bean planets", and new objects
  go into this zone. Esc leaves. Without entering, new objects go into the zone
  whose bounds contain them, or the galaxy itself.
- **Moving a zone** moves everything in it (the zone's own placement in the
  galaxy); its Inspector has the zone's transform and the missions it appears in.
- **Shared zones**: two zones are used by two galaxies each
  (`MysteryGravityRoomZone`: Good Egg and Dusty Dune; `SphereInsideZone`: Good
  Egg and Gusty Garden). On the first edit inside a shared zone, a sheet
  asks: *Edit everywhere* (both galaxies change) or *Make a copy for Good Egg*
  (the galaxy gets its own copy of the zone). The choice is remembered for the
  project.
- **New zone**: from a selection (*Move to new zone*), or empty. Zones are how
  you'd reuse a set piece in several galaxies.

---

## 14. A new galaxy

**File → New Galaxy** (`Cmd+N`), or Level Select → **New galaxy**. A sheet with
three steps and a live preview on the right (the game renders the template).

1. **Start from**
   - *Empty space*: a sky, a start point and nothing else.
   - *One planet*: pick a planet from a strip of thumbnails (sphere, cube,
     disk, donut...); it comes with its gravity.
   - *Starter*: a hub planet with a Launch Star to two more planets and a Power
     Star, fully wired, to remix.
   - *Copy a galaxy*: any galaxy from the game, as a starting point (the copy
     is yours; the original is untouched).
2. **Look and sound**: the sky (the game's skyboxes as thumbnails), the music
   ("music and sounds like Gusty Garden": a galaxy whose soundtrack and sound
   banks the new one borrows, with a play button to preview; TECH.md 5.7), lighting presets
   (taken from galaxies: "Good Egg day", "Ghostly night"...).
3. **Name and place**
   - Name (shown in Level Select and the star list), in English; other languages
     default to it.
   - Where it appears: **Mod galaxies**, a new group at the end of Level
     Select (v1). A place in an observatory dome comes later (TECH.md); the
     sheet shows the domes greyed with "later". The internal name is generated
     from the name (`PipeDreamGalaxy`) and editable under Advanced.
   - Stars collected in a mod galaxy are kept in the mod's own save beside
     your save file, never in the game's save slots.
   - Missions: how many, and their names (editable later in the Missions panel).
   - Stars: one per mission by default.

Everything the community does by hand today happens behind this sheet: the
scenario and zone tables, the galaxy's and missions' names in the message
files, the Level Select entry, and registering each planet used
(`PlanetMapDataTable`), which otherwise makes a placed planet silently invisible.

**Create** writes the galaxy into the project and opens it in Edit, camera on
the start, play marker on the start. The checks list starts with what's left
to make it playable ("Mission 1 has no Power Star").

The **planet palette** is a Browser category ("Planets and terrain") with the
game's planets and big parts as thumbnails, grouped by shape (spheres,
platforms, towers, rings, interiors) and showing their size against a Mario
silhouette on hover. Placing one brings its gravity (smart parts).

---

## 15. Terrain

New collision and geometry is a later milestone (TECH.md), but the editor's
shape leaves room for it now. Sketch:

### Blockout (brushes)

- A **Blockout** tool (`K`) with primitives: box, wedge (ramp), cylinder,
  sphere (planetoid), torus, stairs, arch. Draw a box by dragging a rectangle
  on the ground and pulling up its height (TrenchBroom/Hammer); on a planet,
  the "ground" is the tangent plane at the click.
- Brushes edit with face handles (push/pull), edge bevel, and a clip tool;
  boolean subtract (a brush set to *Cut*) for holes and tunnels.
- Each face has a **surface**: normal, ice, sand, lava, poison, death,
  wall-jump, no-wall-jump, bouncy... (Galaxy's collision attributes, named),
  and a **look** from a palette of the game's own materials (grass, stone,
  wood, metal, as used by real planets), applied by texture lock so it doesn't
  stretch.
- **Planetoid** brushes create their gravity automatically (sphere → point
  gravity, box → cube gravity, cylinder → cylinder gravity), fitted and kept in
  sync as you resize.
- Blockout is playable immediately (collision first, simple shading), so you
  can test a jump before anything is pretty.
- **Bake** turns brushes into a model and collision in the mod, for the final
  look; brushes stay in the project for re-editing.

### Import

- Drag a `.glb`/`.gltf` or `.obj` (and its textures) onto the window, or File
  → Import Model.
- The import sheet shows the model standing next to a Mario silhouette for
  scale, with: scale, up axis, origin; collision (*from this mesh*,
  *simplified*, *from another file*, *none*); material → surface mapping
  (by material name, with a dropdown per material); gravity (*generate*,
  *none*); level of detail.
- The result is a **custom part** in the Browser (with a thumbnail), saved in
  the mod, placeable like any object.

---

## 16. Safety: undo, autosave, checks, changes

### Undo

- **Cmd+Z / Cmd+Shift+Z** undo and redo every change to the galaxy: transforms,
  fields, links, missions, adding and deleting, zones, gravity, paths, names,
  new galaxies. Camera moves and selection are not undo steps (Figma and
  Blender agree on this), but undo selects what it changed and frames it if
  it's off screen.
- Drags, typed values and arrays are one step each; a scrub of a number field
  is one step.
- **History** (Outliner ⋯ → History, `Cmd+Y`): a list of named steps ("Moved
  6 Coins", "Linked Goomba → Switch 7"); click a step to go back to it; the
  steps after it stay (greyed) until you make a new change.
- Play-tests never add steps and never break the history.
- Play-tests can't touch your save: while you edit, the game's saving goes to a
  scratch copy in the project, and your save file is exactly as it was when
  you leave the editor.
- Undo history survives quitting (it's in the journal below).

### Autosave and the journal

- Every change is written to a journal in the project folder as you make it.
  If the game quits or crashes, the next time you open the editor it says
  "Restored your edit of Good Egg from 2 minutes ago" and you're where you
  were, history included.
- **Cmd+S** writes the mod (the archives the game loads: section 17). Until
  then, the mod folder holds the last saved version, so a half-done edit never
  breaks the game for you or anyone else. The toolbar shows "Unsaved changes"
  in small text after the galaxy name.
- **Versions**: every save, and every 30 minutes of work, keeps a version.
  File → Versions lists them with a thumbnail; open one to compare, restore, or
  copy objects out.

### Checks

A **Checks** tab (badge on the toolbar: `⚠ 3`) runs continuously and lists
problems with **click to show** (select and frame the object) and **one-click
fixes**:

| Check | Example | Fixes offered |
| --- | --- | --- |
| Switch with no sender | "Switch 7 is never turned on: 2 objects wait for it" | Pick a sender · Remove the links |
| Switch with no receiver | "Goomba turns on Switch 9, but nothing reacts" | Pick a receiver · Clear |
| Missing path | "Platform follows Path 3, which doesn't exist" | Draw a path · Pick a path · Clear |
| Path too short | "Path 5 has one point" | Add a point · Delete path |
| No gravity | "3 Coins are outside every gravity field" | Drop to ground · Add a field here |
| Out of bounds | "Star is below the death plane" | Move to nearest ground |
| Unreachable star | "Mission 2's Power Star is in no mission's layout" | Show in mission 2 |
| Missing archive | "BigBubble's model isn't loaded in this galaxy" | Add to the galaxy's load list |
| Unknown object | "'Kurib0' isn't a game object" | Pick the right one (closest names offered) |
| Duplicate id | "Two objects have id 66 in Bean planets" | Renumber |
| Not in any mission | "This Coin appears in no mission" | All missions · Delete |
| Memory | "Bean planets uses 104% of the stage's memory" | Show the largest objects |
| Talker with no text | "Toad has no message: talking to him crashes the game" | Write a message · Pick one |
| Launcher with no path | "Launch Star has no flight path: using it crashes the game" | Draw a path · Pick a path |
| Game limits | "161 camera areas: the game allows 160" (also switch, death, water and warp areas 64, BGM areas 32, pipe and tower areas 4, gravity fields 128) | Show them · Merge suggestions |
| Missing required link | "Launch Star #45 needs a path; without one the game crashes when it loads" (each class's required links come from the object catalog) | Draw · Pick · Delete the object |
| Switches used up | "No free galaxy-wide switch numbers (1000–1127)" | Show unused links |
| Too many galaxies | "64 galaxies is the game's limit (48 from the disc plus mods)" | Show mod galaxies |
| Mission has no start | "Mission 3 has no start point" | Place at play marker |

Checks are quiet: a badge and the tab, never a modal. Errors (things that will
crash or soft-lock) are red and ask for confirmation on Share; warnings never
block.

The **memory meter** in the footer shows the stage's heap use against the
headroom the port allows (the editor knows the game's budget). It turns orange
at 90% and red at 100%; hovering shows the biggest contributors. Editing uses
memory too: every re-created object takes a little (a few KB) that's only
given back at the next reset, so a long session without playing creeps up;
Play or Reload brings it back down. Hovering also lists the game's fixed
capacities with their use: gravity fields n/128 (a deleted field keeps its
place until the next reset), objects n/2,560, scene objects n/4,864, collision
parts n/512 per zone, camera data n/1,024. Each becomes a Check near its limit.

### Changes (diff against the original)

The **Changes** tab lists every difference from the disc, grouped by zone:
added (green +), changed (gold ●, with "Position, Turn speed"), removed (red −).

- Click a change to select and frame it; each has **Revert**.
- **Show changes** (`Cmd+\`, toggle): the viewport tints added objects green,
  changed gold, and draws removed ones as red ghosts.
- **Compare** (hold `\`, or hold the toolbar's Compare button): while held, the
  viewport shows the original layout as ghosts over yours (Q7).
- **Revert all in zone**, **Revert galaxy**.

---

## 17. Saving and sharing

### The project and the mod

An edit lives in a **project**: `<user folder>/editor/projects/<Name>/` holding the
journal, versions, editor-only data (switch names, labels, views, hidden and
locked objects, brushes) and the settings. **Cmd+S** writes the playable
**mod** from it: `<user folder>/mods/<Name>/` with `mod.txt` and `files/`
(native/MODS.md). One project can edit several galaxies.

While you edit a project, its mod is on for the session (so Level Select and
play-tests use it), regardless of the Mods page. Other enabled mods stay on;
if one touches the same archives, the editor says so on opening ("Also changed
by mod X; your edits win while editing").

### Share

**File → Share** (`Cmd+Shift+E`, the ⇪ toolbar button) opens a sheet:

- **Name**, **author**, **version** (1.0, auto-bumped on the next share),
  **description** (a few lines; Markdown-free).
- **Screenshot**: the current view without UI, retaken with one click, or
  *Take in photo mode...* to frame one; or choose from `~/Pictures/Petari`.
- **What it changes**: galaxies and zones, generated ("Good Egg Galaxy: 3
  zones; new galaxy: Pipe Dream").
- **Needs**: other mods it relies on (Odyssey movement, Cappy) as checkboxes;
  shown to whoever installs it.
- **Checks**: errors listed with "Fix" links; warnings summarised.
- **Where it plays**: edits to existing galaxies work anywhere the mod is
  installed. A *new* galaxy needs Petari (the original game has no way to add
  galaxies without code patches), and the sheet says so on a line of its own
  when the mod contains one.
- **Export .zip**: writes `<Name>-<version>.zip` to a folder you choose (the
  usual save panel, Downloads by default) and reveals it in Finder.

The zip is the mod folder (mod.txt with the new `author=`, `version=`,
`needs=` lines, `preview.png`, `files/`) plus the editor project (so the next
person can open it in the editor and keep remixing, with names and wires). See
Q5 on whether to ship edited archives or a patch.

### Installing

Drop a mod zip onto the game window (or Home menu → Mods → Mod folder → **Add
mod...**): a sheet shows its screenshot, name, author, what it changes and what
it needs, with **Install** (unpacks into the mods folder and turns it on at the
next start) and **Open in editor**.

---

## 18. Controller

The editor is designed for keyboard, mouse and trackpad, but a controller can
view, play-test and do light editing from the couch.

| Input | Edit mode |
| --- | --- |
| Hold Minus (or the Back/View button) 0.5 s | Enter / leave Edit (a tap stays the game's Minus) |
| Left stick | Fly (move) |
| Right stick | Look |
| LT / RT | Down / up |
| Click right stick | Frame the selection |
| A | Select what's under the centre reticle / confirm |
| B | Deselect / back |
| Hold RB + left stick | Move the selection on the ground (gravity aware) |
| Hold RB + right stick | Turn the selection (heading) |
| X | Duplicate |
| Y (hold) | Radial menu: Play from marker, Resume, Drop to ground, Delete, Undo, Redo, Browser (favourites), Mission |
| D-pad left / right | Undo / redo |
| Plus | Play from marker |

- Selection is by a **reticle** at the screen's centre (the game's Star Pointer
  if the controller has a pointer), with the same hover outline.
- The radial menu is a ring of the game's capsule buttons, chosen with the
  right stick, like a weapon wheel.
- The Browser opens to Favourites as a horizontal strip, chosen with the
  D-pad.
- Full editing (fields, wires) is mouse/keyboard in v1.

---

## 19. Keyboard and trackpad reference

| Key | Action |
| --- | --- |
| Tab | Edit ⇄ Play from marker |
| Shift+Tab | Resume (continue the frozen moment) |
| Hold Tab, Cmd+Shift+Tab | Play from the mission start |
| K | Keep: copy selected live objects' current places into the layout |
| Cmd+R | Reload: rebuild the world from the layout, applying pending changes |
| M | Play marker here |
| Esc | Deselect / leave zone / cancel |
| Q W E R | Select · Move · Rotate · Scale gizmo |
| G, Shift+R, Shift+S | Quick grab / rotate / scale (then X Y Z, a number, Return) |
| F, Shift+F | Frame selection / all |
| End, Shift+End, Cmd+End | Drop to ground · Stand up · Both |
| Page Up / Page Down | Lift / lower one grid step |
| Cmd+D, Cmd+Shift+D | Duplicate (repeat offset) · Array |
| Delete | Delete |
| H, Option+H, Cmd+Shift+L | Hide · Show all · Lock |
| Space | Quick add at cursor |
| B, I | Browser · Inspector (in Focus mode) |
| P | Pen (paths) / paint |
| K | Blockout (terrain milestone) |
| T | Test fall (gravity view) |
| 0 | Look through camera |
| 1–7 | Visibility: gravity, areas, cameras, paths, links, starts, hidden |
| U | Up: gravity / world |
| [ ] | Grid size |
| , . | Gizmo frame · pivot mode |
| Hold \, Cmd+\ | Compare with the original · Show changes |
| Cmd+Z, Cmd+Shift+Z, Cmd+Y | Undo · Redo · History |
| Cmd+S, Cmd+Shift+E | Save mod · Share |
| Cmd+N | New galaxy |
| Cmd+K | Command search (every command, object and setting) |
| Cmd+. | Focus mode |
| Cmd+[ / Cmd+] | Previous / next view |
| Option+1–9 | Saved views |
| Cmd+1–5 | Snapping toggles |

Every binding is remappable on the Home menu's Controls page (a new "Editor"
section), and **Cmd+K** finds any command by name with its key shown, so nobody
has to learn this table.

Trackpad: two-finger swipe orbits, Shift+swipe pans, pinch zooms to the cursor,
three-finger swipe goes back/forward through views, force click (or Ctrl-click)
opens the context menu on an object (Frame, Drop to ground, Duplicate, Select
same kind, Missions, Delete, See examples).

---

## 20. Milestones and open questions

### Suggested order (UX view; TECH.md has the engineering plan)

1. **Look and move**: Edit/Play toggle, editor camera, selection, Inspector with
   transforms and raw fields, drag on ground, gizmos, undo, save to mod,
   Changes. This alone beats Whitehole's loop.
2. **Understand**: named fields from the catalog, wires for switches and
   paths, gravity view, mission picker and chips, Checks.
3. **Create**: Browser with thumbnails, smart parts, arrays, paths, cameras
   with preview, new galaxy flow, Share.
4. **Terrain**: blockout, then import.

### Open questions for the user

- **Q1. Tab for Edit/Play.** Tab is free in play today. Is it the right key,
  or should the editor have to be switched on first (Home menu → Mods → Level
  editor) so a stray Tab never freezes a normal game? Proposal: off until turned
  on once, then Tab.
- **Q2. Galaxy font for data.** The game's MessageFont is lovely at title sizes
  and soft at 12 px. Proposal: Galaxy lettering for titles, tabs and buttons; a
  crisp UI sans for field names and numbers (macOS's system font, read from the
  system at runtime, not shipped). Or Galaxy font everywhere at a larger size
  (less fits)?
- **Q3. Panels beside or over the game.** Proposal: docked beside it (the game
  image shrinks), with Focus mode for overlays. Some editors (Mario Maker,
  Dreams) overlay everything on a full-screen game.
- **Q4. Community object database.** The community's object database
  (galaxydatabase, used by Whitehole) has years of argument names, but it has
  no licence (and about a quarter of its 900 classes are undocumented).
  Proposal: our own catalog, generated from the decompilation (which argument
  each class reads, its type and default: about 600 read sites) with hand-written
  names and help for the ~150 most used objects first. Optionally, an **Import
  community names** button that downloads the database into your user folder
  (as Whitehole does), and we ask its maintainers about bundling. OK to reach
  out?
- **Q5. Share edited archives or a patch?** A shared mod normally contains the
  edited stage archives (the community norm). A patch (only the changes,
  applied to each player's own disc at load) is smaller and contains no
  Nintendo data, but needs our loader to apply it. Proposal: patch by default,
  "full archives" as an option for other tools (TECH.md).
- **Q6. Play-tests and the save.** Proposal: play-tests use a scratch copy of the
  save and never save stars. Do you also want a "play it for real" mode that
  plays your mod as the game (with saving), or is enabling the mod and using
  Level Select enough?
- **Q7. Compare key.** Hold-to-compare wants a key you can hold. Backquote is
  the Home menu. Proposal: hold **`\`** to compare, `Cmd+\` to toggle the
  change tint.
- **Q8. Names in other languages.** New galaxy and mission names can be given
  in each of the disc's three languages, falling back to English (TECH.md 5.7).
  Should the sheet ask for all three, or English only with the others under
  Advanced (proposed)?
- **Q9. Supernova.** SMGCommunity's new editor (MIT, early alpha) already uses
  Petari as its reference for simulating objects. Should our catalog and
  formats aim to be shareable with it, or stay independent?

---

## Appendix A: research notes

The full notes, with sources, are in [research.md](research.md): the
Whitehole family (original, Despaghettification, Neo, and the new Supernova),
19 verified community pain points with quotes and issue links, what a typical
edit and a new galaxy take today (up to ten separate tools), the borrowed
mechanics with references, and the Galaxy vocabulary (fields, switch ranges,
gravity rules, camera types, layers) checked against the decompilation.

The five that shaped this design most:

1. **No live testing.** Every edit is save, copy, cold-boot Riivolution and
   Dolphin. Hence Tab.
2. **Silent crashes** from configuration mistakes and hidden limits. Hence
   Checks that know every crash cause.
3. **Linking by matching numbers** across objects, with several names for the
   same field. Hence wires and automatic switch numbers.
4. **Blind gravity, trajectories and cameras** ("look at the green axis line";
   "extremely tedious to figure out where Mario will land"). Hence the gravity
   view, Mario's trail, and camera preview.
5. **No undo for a decade, and save corruption.** Hence undo for everything,
   the journal, and never writing the disc.
