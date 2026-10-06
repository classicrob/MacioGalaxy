# Level editor: the big UX decisions

One page. Details are in [UX.md](UX.md), with a clickable mockup in
[mockups/index.html](mockups/index.html) (open it in a browser; Tab toggles
Play) and the research in [research.md](research.md).

**The pitch.** Press **Tab** anywhere in the game. The world freezes where it
is, panels slide in, and you're editing the galaxy you were standing in. Press
**Tab** again to play what you made. Whitehole's loop is save, copy files,
reboot Dolphin with Riivolution, then walk back to the spot. That's minutes per
test. Here it's one key.

| # | Decision | Alternatives considered | Why |
| --- | --- | --- | --- |
| 1 | **Tab toggles Edit and Play.** Entering Edit never changes what's on screen (the frame freezes, as in photo mode). Play always starts from your layout, at a draggable "play marker" Mario, after a ~2 s reload behind the game's star wipe (instant when nothing changed). Coming back draws Mario's trail. Hold Tab to play from the mission start | Reset the world the moment you enter Edit; play only from the mission start (as in Whitehole); a separate editor app | Mario Maker showed that free testing makes good levels. Keeping the frame lets you study the jump you just missed |
| 2 | **Gravity is the default frame.** Dragging an object slides it over the ground and keeps it upright, and End drops along gravity. The camera's "up" is the gravity at the point you orbit around | World X/Y/Z gizmos with a gravity option (Whitehole, Blender) | On a round planet, world axes are the main reason objects end up floating, buried or tilted |
| 3 | **Human names, raw within reach.** "Behaviour: Talks, then hides" instead of `Obj_arg1 = 1`, with one line of help on hover. An Advanced fold always shows the real fields | Raw fields only, plus a wiki (today); human names only, hiding the raw | Newcomers can read it, and Whitehole tutorials still work |
| 4 | **Links are wires.** Switches, paths and cameras are drawn as wires between objects in 3D, and you make them by dragging a socket onto an object. Switch numbers are picked automatically | Typing matching numbers into separate objects (today); a node-graph window (Unreal Blueprints) | Galaxy's logic is in space, so the wires belong in the scene, not in a second window |
| 5 | **Missions, not layers.** A mission picker fades out what isn't in that mission, and each object has mission chips (`All 1 2 3 4C 5C 6H`). Layer letters and bitmasks never appear outside Advanced | Show the layers (common, layerA–P) and hand-summed masks (today) | Players think "this appears in mission 2". The masks are easy to get wrong |
| 6 | **Show the invisible.** A gravity view draws every field with flowing arrows and a probe that names the winner, and marks ground with no gravity in red. Camera areas show a live preview of the camera they give | Draw shapes only when selected (the forks); test and see | Gravity is what makes Galaxy Galaxy, and it's the most blind part of editing today |
| 7 | **Checks that know every crash.** A badge, never a modal. Each check has click-to-show and one-click fixes: a missing path or message, a switch nobody turns on, ground with no gravity, game limits (160 camera areas...), memory | Validate on save; no validation (today: silent crashes in game) | The game crashes on many small mistakes with no message |
| 8 | **Galaxy's look, a crisp type for data.** The Home menu panels, the pause screen's teal, and gold for the selection (as for a focused button). The game's lettering for titles, tabs and buttons; a small UI sans for numbers and field names. Docked panels beside the game, with a Focus mode | Galaxy font everywhere; panels overlaid on a full-screen game | The disc font is a 26 px bitmap and goes soft at 12 px. Overlays hide the level you're editing |

**Agreed with the tech lead** (TECH.md): edits are live while frozen. A moved
object moves, a changed setting re-creates that object in under a millisecond,
and adding one is instant when its model is loaded. Zones, cameras, mission
membership, starts and lighting need a quick reload, which Play does anyway.
The tech lead's spikes created Octoombas, Coins, Launch Stars, Boos and Dry Bones
live in Good Egg, loading models that weren't in the stage in under 2 ms. We
agreed the reload happens on Play, and coming back to Edit is an instant freeze
that keeps the frame.

## Decisions (the user, 2026-10-02)

The user approved the design and took the recommendation on every open question:

1. **Tab** toggles Edit and Play, but the editor stays **off until switched on once** in the Home menu (Mods page, "Level editor"). A stray Tab never freezes a normal game.
2. **Type:** Galaxy lettering for titles, tabs and buttons; a crisp system UI font for data.
3. **Panels** are docked beside the game, with a Focus mode.
4. **Object database:** our own catalog, built from the decompilation. An optional "import community names" button downloads theirs into the user folder. Contacting the maintainers is deferred; the lead will ask the user first.
5. **Sharing:** patch mods by default (no Nintendo data); Full-archive export as an option.
6. **Play-tests** run on a scratch save. Enabling the mod and using Level Select is the "play it for real" path.
7. **Mod galaxies** get their own Level Select group in v1; observatory domes come later.
8. **Supernova:** share formats and the catalog where practical.
9. **Technical** (TECH.md section 9):
   - New galaxies are Petari-only; edits to existing galaxies stay portable.
   - Mesh gravity is deferred; terrain gets fitted game gravity shapes.
   - No VoiceOver in v1.
   - Vendoring MIT/Apache libraries (Manifold, cgltf, tinyobjloader, meshoptimizer) is fine when needed.

The build plan is TECH.md section 7. Work happens on the `level-editor` branch, with stacked `editor/<topic>` branches merged into it.
