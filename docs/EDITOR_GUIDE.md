# Make your first level

Enable **Level editor** on Petari's **Mods** page, enter a galaxy, then press **Tab**. The game freezes and the editor opens. Try a small change first: place a Coin, move the play marker with **M**, and press **Tab** to try it. Press **Tab** again to return to Edit.

Click **?** in the toolbar for keys and five quick tips. Most shortcuts wait while you type in a field. **Tab** finishes the field edit before playing; **Cmd+S** finishes it before saving. Modal sheets keep Tab from starting play.

## Look, select, move

**Option-drag** or **middle-drag** orbits; add **Shift** to pan. A mouse wheel or pinch zooms. Trackpad scrolling orbits, **Shift-scroll** pans, and **Option-scroll** zooms. Hold the right mouse button to look, with **W/A/S/D** to fly and **Q/E** to descend/climb; **Shift** speeds up flight. The camera follows local gravity; **U** toggles world-up.

Click an object, or select its row in the **Outliner**. **Shift-click** adds to the selection; **Cmd-click** toggles membership. **F** frames the selection (Mario if nothing is selected). **Cmd+.** hides or restores the panels.

Use **W** to move, **E** to rotate, **R** to scale and **Q** to select without a transform tool. Drag the handles; **comma** cycles the gizmo's gravity, local and world frames. Hold **Cmd** during a drag to suspend snapping. **End** drops each selected object onto the ground along its gravity; **Shift+End** stands it upright; **Cmd+End** does both. On compact Mac keyboards, End is **Fn+Right**.

## Add things and tune them

In the **Browser**, search for an object or choose a category. **Click a card, then click the ground** to place it; you can also drag the card onto the ground. Click the armed card again or press **Esc** to stop placing. Some cards need an example already present in the galaxy, or required links before the object can work. Copies clear optional switches and other links by default; use **Keep links** only to reuse that wiring. Required paths retain their zone. Read the placement feedback and **Checks**.

The **Inspector** shows transforms and named settings for the selection. Hover fields for help; expand **Raw fields** only when you need the original game values. The **Changes** tab shows edits and history. **Cmd+Z** undoes, **Cmd+Shift+Z** redoes. Most edits appear immediately; the **Reload … pending** count tells you when the scene needs rebuilding.

## Paths, switches and invisible things

- **New path:** use the Browser's **New path** button or **View → New path**. Click ground points, then **Enter** to finish (at least two points). **Backspace** removes the last draft point; **Esc** cancels. Select the curve's points and drag them or their Bézier handles; **Option-click** the curve to insert a point. Delete a selected point, or use **Insert after / Delete point** in the Inspector. Creation is one undo step.
- **Launch Star:** place and select a Launch Star, then click **New flight path** in its Inspector. The draft starts at the star. Click a landing, optionally adding intermediate points, then Enter. This creates and links its own path in one step. Use **Checks** for missing paths, facing and capacity problems; play-test the flight. A galaxy without the canonical path-table template may refuse new paths and explain why.
- **Link an existing path:** drag the object's **Rides path** socket onto the curve. Editing a rail refreshes supported followers on commit; if a class cannot refresh safely, its notice asks for Reload.
- **Switch wires:** open **Links** in the Inspector, or hover the object's compact socket handle in the viewport. Drag a socket onto an object to connect it; IDs are assigned automatically. Input/output labels explain the roles. Connected sockets stay visible as colored wire ends; unused sockets stay collapsed. Hover a wire for its switch name. The Inspector's **x** unlinks, with undo. **5** cycles the paths/wires view.
- **Gravity:** **1** or **View → Gravity fields** reveals field outlines, arrows and the cursor probe. Select a field to change its shape, range and priority. Move/rotate/scale its bounds; a wire field follows its linked path. Use the probe and a play-test to check which field actually pulls Mario.
- **Areas and cameras:** reveal these in **View**, or use **2 / 3** to switch between all and selected volumes. Click an outline, then move/rotate/scale it or edit its dimensions and named settings. Selecting a camera area offers a **Camera preview** from the play marker (area centre when the marker is outside) or Mario. It shows scene models; effects and postprocessing appear in Play. **Camera edits need Reload before they affect play.**

![Drafting a three-point path on the ground](../build/new-path-styled-live/surfaces/surface-new-path-draft-0.png)

[Camera-area preview capture (earlier toolbar layout)](../build/areas-live-play-hooks/surfaces/surface-areas-camera-preview-5.png)

## Build platforms and repeated layouts

**Brushes** are editable terrain. Search the Browser for a Box, Sphere, Cylinder or Ramp brush. Place it, resize with the gizmo, and choose its **Shape**, **Material**, **Surface** and **Gravity** settings. Collision and supported gravity update live. Fitted gravity follows the shape; Down only pulls toward its top; Stage uses the galaxy's existing fields.

**Array** is under **Arrange** in the Inspector, or **Cmd+Shift+D**. Choose Line, Arc, Ring, Grid or Along a path; adjust copies, spacing or radius while watching the ghosts. Line has colored **Forward/Back, Right/Left, Up/Down** buttons relative to gravity. Up/Down turns ground snapping off so copies keep their elevation. **Snap each copy to ground** settles copies individually. The handle changes count or radius; **F / Frame preview** fits the whole arrangement, and an offscreen handle reframes after an edit or a finished drag. **Apply array** commits one undo step; **Esc** cancels. Selecting another object also applies the preview.

![Line array with visual direction choices and its end handle](../build/array-direction-visible/surfaces/surface-array-axis-0.png)

**Import model…** is in the Browser and the **Level Editor** title menu; you can also drop an OBJ, glTF or GLB file onto the window while in Edit. Choose scale, up-axis and origin in the preview. Map surfaces to imported textures/colors or game materials, and choose full, simplified, bounds-only, no collision or a separate collision model. Review triangle/texture warnings and optional detail levels/gravity. **Import at play marker** places it and adds a reusable Browser card. Static geometry and base color are supported; animated rigs and full PBR materials are not reproduced. Assets travel with a saved patch; terrain and imports bake into game models for play.

## Missions and play-tests

**View → Play view (4)** opens a draggable camera preview from the play marker, or Mario if there is no marker. It follows the camera area, ground camera or default camera there, independently of your selection. Read the label and any fallback message: it previews scene models, without sky, effects or full lighting; some special cameras need live gameplay. Use **Select** to inspect its camera area when available.

The numbered toolbar buttons switch missions and rebuild the scene. Hover them for mission names. **View → Other missions' objects** shows ghosts. In the Inspector's **Appears in** section, choose mission chips and **Apply** or **Cancel**. **Only this mission** applies immediately. One change is one undo step; matching copies in other missions change together. If existing layers cannot represent your choice, Apply explains the allowed combinations instead of changing objects. A new Browser copy from an off-mission layer is placed in the common layer, so check where it appears.

Point at the ground and press **M** to set the play marker. **Tab / Play** rebuilds and starts there; **Shift+Tab / Resume** continues the frozen moment; **Option+Tab / From start** begins at the mission start. Return with Tab to adjust the layout.

Enable **View → Show trail** to see the last 60 seconds: green on ground, blue in air, orange diving, purple cap bounce, gold jump peaks, red at a miss. A death or fall returns to Edit with **Missed at…**, preserving the trail; retry rebuilds the scene. Collecting a Power Star returns to Edit with a clear time. Select a supported object that moved during play and press **K / Keep played transform** to copy its current transform into the layout as one undo step. The Inspector explains when Keep is unavailable. Returning to Edit alone does not keep those transforms. Editor play-tests do not save earned progress: stars, coins, lives and unlocks are restored when the editor session ends. To play for real, save the mod and play it with the editor disabled.

## Save, share, and start a galaxy

Open **Checks** before saving: click a problem to find it and use a suggested fix when offered. It highlights problems introduced by your edits, including missing links and capacity limits.

**Cmd+S / Save** opens the save sheet the first time and saves to the current mod afterwards. **Patch (recommended)** stores edits and your assets; **Full archives** exports rebuilt game archives for other tools and includes game data. **Cmd+Shift+S** changes the name or format. Save does not overwrite the original disc. Reload refreshes the scene; it is not a save.

On Edit entry after an interrupted session, **Restore N unsaved edits…** asks you to **Restore** or **Discard** the journal. Restore reloads the recovered layout, still unsaved; use Cmd+S to keep it. The prompt stays open until answered.

**Share** packs the saved mod as a zip with name, author, version and description. Save your latest changes first. To install, drop the zip onto Petari, review the sheet and confirm; replacing an installed version asks again. Enable the mod on the Mods page and restart when requested. Share only assets you have permission to distribute; keep Full archives private.

**Cmd+N / Level Editor → New galaxy…** starts from Empty space, One planet, Brush skeleton or Copy a galaxy. Choose a sky, music, a name and 1–8 named missions. Save or discard current edits before Create. Petari restarts into the new galaxy in Edit; it is also listed under **Mod galaxies** in Level Select, which has a **New galaxy…** entry at the end when the Level editor switch is on. Choose it, then **Create…**, to open the sheet from the Observatory. Each mission starts with its Power Star (except in Empty space); Checks lists what is still missing, such as a star or ground under Mario's start. In a new galaxy, the **Missions** panel renames the galaxy and its missions and changes its music straight away (the music plays from the next reload). Draft settings with **Apply / Cancel**. Changing the mission count shows **Apply (restarts)** and asks you to save or explicitly discard unsaved layout edits first. Removing trailing missions leaves their exclusive objects in the layout but hidden from all remaining missions; adding those missions back reveals them. New galaxies play only in Petari. Their progress uses the mod's own save.

## Shortcut reference

**Cmd** = Command, **Option** = Alt. Camera gestures start over the viewport. Tools and help use Esc before it clears the selection.

| Key / gesture | Action |
|---|---|
| **?** (Shift+/) | Open/close Keys and tips |
| **Tab** | Enter Edit / play from marker |
| **Shift+Tab** | Resume frozen play |
| **Option+Tab** | Play from mission start |
| **M** | Place play marker on ground under cursor |
| **Right-drag** | Look |
| **Right held + W/A/S/D** | Fly forward/left/back/right |
| **Right held + Q/E**; **Shift** | Fly down/up; faster flight |
| **Option-drag / middle-drag** | Orbit |
| **Shift+Option-drag / Shift+middle-drag** | Pan |
| **Mouse wheel / pinch** | Zoom |
| **Trackpad scroll / Shift-scroll / Option-scroll** | Orbit / pan / zoom |
| **Click / Shift-click / Cmd-click** | Select / add / toggle selection |
| **Q / W / E / R** | Select / move / rotate / scale (unless flying) |
| **,** | Cycle gravity/local/world gizmo frame |
| **Cmd during drag** | Suspend snapping |
| **End / Shift+End / Cmd+End** | Drop / stand upright / both |
| **F** | Frame selection or active Array preview; Mario if none |
| **U** | Toggle world-up camera |
| **Cmd+.** | Toggle Focus mode (hide panels) |
| **Delete / Backspace** | Delete selection, selected path point or wire |
| **Cmd+D** | Duplicate selection |
| **Cmd+Shift+D** | Array |
| **Cmd+Z / Cmd+Shift+Z** | Undo / redo |
| **1** | Toggle gravity view |
| **2 / 3** | Areas / cameras: all versus selection |
| **4** | Toggle Play view preview |
| **5** | Cycle paths/wires: selection, all, hidden |
| **Option-click curve** | Insert path point |
| **Enter** while laying path | Finish draft |
| **Backspace / Delete** while laying path | Remove last draft point |
| **K** | Keep supported played transforms |
| **Cmd+R** | Reload and apply pending edits |
| **Cmd+S / Cmd+Shift+S** | Save / Save as |
| **Cmd+Shift+E** | Share |
| **Cmd+N** | New galaxy |
| **Esc** | Close help, cancel a tool/placing, or deselect |

The screenshots above are local captures under `build/`, referenced here but not committed. The guide works without them.
