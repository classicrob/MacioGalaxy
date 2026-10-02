# Research: UX for an in-game SMG level editor (Petari)

Compiled 2026-10-02. Sources are linked inline. "Verified in decomp" means checked against the local Petari source (`src/Game/Map/StageSwitch.cpp`, `src/Game/Util/GravityUtil.cpp`, `src/Game/Gravity/PlanetGravity.cpp`).
Field data for objects comes from the galaxydatabase `objectdb.json` (raw file downloaded from GitHub; 2,446 objects and 900 classes).

---

## Part A: Whitehole and its forks

### A1. Lineage

| Editor | Who / status | Notes |
|---|---|---|
| Anarchy in the Galaxy | unknown author, ~2010–11 | Came before Whitehole. It had no 3D view. ([mariogalaxy.org](https://www.mariogalaxy.org/wiki/Whitehole)) |
| Whitehole 1.0–1.4 | StapleButter/Arisotura, with help from Dirbaio, Treeki and others; started 2012 | Java + JOGL. Original 1.x needed JRE 7. ([Arisotura/Whitehole](https://github.com/Arisotura/Whitehole), [Kuribo64 manual](https://kuribo64.net/board/thread.php?id=2702)) |
| Whitehole 1.5–1.7 | TheSunCat, IonicPixels, Lord-Giganticus | Added an MSBT editor, a CANM editor (work in progress), a camera previewer, undo/redo, Blender-style shortcuts, a "Riivolution-style" mod folder and an OBJ importer. 1.6 needs JRE 8 and 1.7 needs JRE 17. ([TheSunCat/Whitehole](https://github.com/TheSunCat/Whitehole)) |
| Whitehole for SMG1 | community | SMG1 only and can't be updated. Kept around for its built-in Zone-Layer editor. ([mariogalaxy.org](https://www.mariogalaxy.org/wiki/Whitehole)) |
| Whitehole Despaghettification | SunakazeKun (Aurum); archived 2024-01-04 | A "major rewrite" that removed broken features from 1.4–1.7. Added FlatLaf light/dark UI, the new object DB, plain-English galaxy names and full SMG1 compatibility. Targets Java 11. ([repo](https://github.com/SunakazeKun/Whitehole-Despaghettification)) |
| **Whitehole Neo** | SMGCommunity (AwesomeTMC, SuperHackio, Lord-Giganticus and others); active, last push Aug 2026 | Built on Despag. Adds undo, copy/paste for all objects, a SMG2 worldmap editor and better rendering. Java 11+, but needs `--add-exports/--add-opens` flags or a .bat launcher. ([repo](https://github.com/SMGCommunity/Whitehole-Neo)) |
| Whitehole Geo | TheProtaganist | Neo fork with natural-language "AI Commands" (for example "add a Goomba at 100,0,200" or "move all coins up by 5"), pattern generation such as coin rings and spirals, and Shift+LMB orbit. ([repo](https://github.com/TheProtaganist/Whitehole-Geo)) |
| Whitehole Pro | "Super-Mario-Galaxy-PRO-Modding" | Pitched at beginners: a two-step Windows setup and a bundled sample map, so no game dump is needed. Uses AI tooling. The same group's AI-generated C++20 rewrite PR to Neo (#116) was closed with "Ai is NOT allowed on this repository". ([PR #116](https://github.com/SMGCommunity/Whitehole-Neo/pull/116)) |
| **Supernova** | SMGCommunity; MIT; C#/.NET 8, ImGui + OpenGL; early alpha since Feb 2026 | Simulates actors, rails and RailMoveObj/RotateMoveObj live, using Petari as its reference for behaviour. Per-object typed editors with checkboxes, dropdowns and Obj_arg tooltips. Includes camera editing with a dummy player, an intro-camera keyframe editor, a scenario/layer editor, a demo timeline and LightData editing. Projects write to their own output directory, and the format is written back 1:1. README warns of "a real risk of losing level progress". ([repo](https://github.com/SMGCommunity/Supernova)) |
| Takochu (shibbo), Starlight / Blackhole (SuperHackio) | abandoned | Earlier attempts to replace Whitehole that never shipped. ([mariogalaxy.org](https://www.mariogalaxy.org/wiki/Whitehole)) |

### A2. Whitehole Neo UI structure

Sources: the labels in [GalaxyEditorForm.java](https://github.com/SMGCommunity/Whitehole-Neo/blob/master/src/whitehole/editor/GalaxyEditorForm.java) and [AbstractObj.java](https://github.com/SMGCommunity/Whitehole-Neo/blob/master/src/whitehole/smg/object/AbstractObj.java), plus the README.

- **Main window:** a list of galaxies. Neo shows plain-English names, and holding Shift shows internal names (closed issue #28).
- **Galaxy editor (Swing):**
  - **Left column, top to bottom:**
    - "Scenarios:" list
    - " Zones: " list
    - "Layers:" checklist (Common, LayerA…P)
    - Object tree, grouped by type: Objects, MapParts, Areas, Gravities, Cameras, Paths, Spawns (Start), Cutscenes, Sound Objects, Sound Areas, Light Areas, ChildObjs, Positions, Debug
    - Property grid (`PropertyGrid`) at the bottom
  - **Right:** JOGL 3D view
  - **Toolbar:** Save, Close, Add object, Delete object, Copy/Paste, "Edit individually" (opens one zone on its own), and toggles for Show paths / areas / gravity / cameras / axis
- **Property grid categories:**
  - General: Object name, Zone, Layer
  - Transform: X/Y/Z position, rotation and "size"
  - Obj_arg0–7, labelled from the object DB
  - Switches, as a `switchid` field type
  - Group_Move / Group_Rotate / Group_Copy panels for multi-selection, where you type step/offset numbers
- **Raw field names relabelled by Neo:**
  - l_id → "Link ID"
  - Obj_ID → "Linked Object ID"
  - MapParts_ID → "Linked MapParts ID"
  - ChildObjId → "Linked Child ID"
  - FollowId → "Linked Area ID"
  - GroupId → "Group ID"
  - ClippingGroupId → "Clipping Group ID"
  - ViewGroupId → "View Group ID"
  - DemoGroupId → "Cutscene Group ID"
  - CastId → "Cast Group ID"
  - CommonPath_ID → "Path ID"
  - ShapeModelNo → "Model ID"
  - CameraSetId → "Camera Set ID"
  - MessageId → "Message ID"
  - ParamScale → "Speed Scale"
  - GeneratorID/ParentID → "Generator Object ID"
  - AreaShapeNo → "Shape ID"
  - Fields that are unused for the object get a greyed "unused" wrapper.
- **Controls** ([README](https://github.com/SMGCommunity/Whitehole-Neo)):
  - Mouse and camera: LMB click selects (Shift/Ctrl add to the selection). LMB drag pans the camera or moves the object. RMB drag orbits. Wheel dollies the camera or moves the object along depth. Arrows and PgUp/PgDn (or WASD/QE) move the camera.
  - Transform: hold G, R or S to move, rotate or scale.
  - Clipboard and undo: Ctrl+C. Ctrl+V pastes at the mouse and Ctrl+Shift+V at the original position. Ctrl+Z undoes.
  - Selection and view: Shift+A opens quick add. Space frames the selection and Shift+Space frames the zone. H hides, Alt+H unhides all, Del deletes.
  - Path and number tools: Ctrl+N truncates decimals. Ctrl+Shift+R reverses path points.
  - Worldmap: L links worldmap points and P toggles pink/yellow.
- **Other tools in the same app:** BCSV editor, hash generator, galaxy/zone properties, create-galaxy form, and the SMG2 worldmap editor. Discord Rich Presence was a source of bugs.

### A3. Features and gaps, by area

- **Object DB:** the editor loads `objectdb.json`/`.xml`. Despag re-downloaded it on every launch. A missing DB made older versions fail with "Failed to load object database!" ([Kuribo64 #3838](https://kuribo64.net/board/thread.php?id=3838))
- **Rendering:**
  - Neo renders BMD models, with special renderers for some objects: PlantGroup, Black Hole range, invisible walls and AstroDomeEntrance (closed issues #14–16, #26).
  - Still missing: FireBar, ClipArea objects, Matter Splatter, trajectory arcs and collision visualisation (open #72, #95, #67, #64, #63; #58 closed).
  - "Despag does not render gravity areas" ([mariogalaxy.org](https://www.mariogalaxy.org/wiki/Whitehole)). Neo adds "Show gravity".
- **Paths:** paths are separate objects with points and two control handles each. You set them through Path ID (CommonPath_ID) on the object, plus Path_argN and Point_argN.
  - Bugs: "Whitehole saves path files incorrectly" (#41); "Path IDs with the same ID are sometimes shown in ALL zones" (#20); "Adding a non-path and then a path in the same zone causes problems" (#88); Shift-click to place path points broke (#43).
  - The older Kuribo64 tutorial makes Launch Star paths by placing temporary StrongBlocks as markers, copying their coordinates into Point 1–3 by hand, then deleting the markers. ([Kuribo64 manual](https://kuribo64.net/board/thread.php?id=2702))
- **Cameras:**
  - Whitehole 1.5.4 added an XZ_PARA generator and a previewer.
  - Otherwise people use Launch Cam Plus, which has "no visual feedback" ([Luma's Workshop Tools page](https://www.lumasworkshop.com/wiki/Tools_and_Resources), archived; [LCP](https://github.com/SuperHackio/LaunchCamPlus)).
  - Open requests: Camera Previews (#82), and showing the camera's world position (#105).
- **Scenarios / layers:** Neo shows scenarios and layers. SMG2 scenario editing is done in Scenaristar (SuperHackio) or Dossun2.
  - Bugs: "Scenarios in the list don't show up sometimes" (#48); jumping to zones that aren't in the current scenario (#49).
- **Zones:** shown as a list and placed in the galaxy as StageObj. Creating a zone is still an open request (#57). Zones can be opened with "Edit individually", which broke picking (#9, #10).
- **Undo:** didn't exist in the original or Despag. Neo added it, but "Undo is extremely buggy… Undoing a multi-selection just completely breaks the program", with objects deleted (#7, fixed). Redo is still an open request (#38).
- **Validation:** Neo added "Warn the user when something that's required is not set" (#6), which shows when you close the galaxy, and an AreaObj limit checker (#31). Still open: Pipe ID max warning (#101), and Link/Group/Switch ID generators (#29). Switch generation "always starts from 0 and finds the smallest unused switch ID" (#71 asks for a Switch Manager).

### A4. Community pain points (verified, with quotes)

1. **Java setup:**
   - TheSunCat README: "WHITEHOLE REQUIRES A VERY SPECIFIC JAVA SETUP!" It lists Oracle JRE 8 and JDK 11, then tells Windows users to install Adoptium 17 with JAVA_HOME set, "may need to reboot". ([TheSunCat/Whitehole](https://github.com/TheSunCat/Whitehole))
   - Neo needs `java --add-exports=java.desktop/sun.awt=ALL-UNNAMED --add-opens=java.desktop/sun.awt.windows=ALL-UNNAMED -jar Whitehole.jar`.
   - mariogalaxy.org gives a .bat workaround for galaxies not opening or "part of the rendering screen… is blue".
   - Windows Defender and VirusTotal flag downloads (#86, #80).
2. **GPU / platform rendering:**
   - "Whitehole 1.7 does not work well under Intel Integrated Graphics" ([mariogalaxy.org](https://www.mariogalaxy.org/wiki/Whitehole)).
   - Open issues: Parallels/Win11 VM rendering issues (#115) and "Some galaxies may not be editable on unix based systems" (#87).
   - Other issues: High-DPI text "extremely small" (#69, #25); black screen on open (#42).
   - On Kuribo64, the same build gave a black screen on a friend's PC but worked on another.
3. **Crashes, both editor and game:**
   - Editor: "whitehole is crashing randomly and needs fixing also some level do not load at all" (#96, Feb 2026).
   - In game: the Kuribo64 reply to "the game just crashes" lists 9 likely causes ([K64 #3598](https://kuribo64.net/board/thread.php?id=3598)):
     - (a) editing an unsuitable galaxy such as HeavensDoorGalaxy
     - (b) an object configured incorrectly
     - (c) a broken scenario file
     - (d) "SMG1 is a horrible game for hacking"
     - (e) bad extraction
     - (f) broken zones or layers
     - (g) a deleted power star
     - (h) bad AppearPowerStarObjs
     - (i) too many objects
   - Other crash advice: NPCs without text crash the game; Launch Stars without a path crash ([K64 #3257](https://kuribo64.net/board/thread.php?id=3257)). "Toads and main characters will crash the game if talked to without it [dialogue]" ([K64 manual](https://kuribo64.net/board/thread.php?id=2702)).
4. **No feedback until you're in the game:** these crashes are silent configuration errors that only appear after you rebuild and boot Dolphin.
5. **No hot reload:** saving while the game runs usually triggers "Disc Not Read", unless the .arc stays exactly the same size. Issue #51 proposes padding archives (a "Cardboard Box") so edits can be tested without reloading ([#51](https://github.com/SMGCommunity/Whitehole-Neo/issues/51)).
6. **Save corruption:**
   - 1.5.4 changelog: "Fixed map corruption when cancelling a save".
   - "BCSV Editor Saves Incorrect Data" (#113, open).
   - Neo: "Prevent saving over BCSVs when clicking Save instead of Open" (#47).
   - Supernova warns of "a real risk of losing level progress".
7. **Undo/redo:** see A3. There was no undo for about 10 years. Neo's undo was "extremely buggy" (#7), and redo is still missing (#38).
8. **Obj_args unexplained:** "Tooltips for Obj_args and BCSV fields" (#52, open); "Add some way to show the obj_arg descriptions from the ObjectDB" (#19); "Obj_args with Float type" (#75). In the DB, **225 of 900 classes have Progress 0**, meaning undocumented.
9. **Inconsistent names for the same field:** the Luma's Workshop Silver Star tutorial has to write "l_id (aka: ID, Link ID)", "ParentID (aka: Linked Parent ID)" for SMG1, and "GeneratorID (aka: Parent Group ID)" for SMG2 ([Silver_Star_Setup_Tutorial](https://www.lumasworkshop.com/wiki/Silver_Star_Setup_Tutorial), archived).
10. **Linking by matching integers:**
    - Warp pads connect by sharing a GroupID. Silver Stars link by `ParentID`/`GeneratorID` = the CollectTico's `l_id`. Switches link by matching numbers.
    - There's no visual wire, and the requested Switch Manager (#71) would only list the IDs.
11. **Blind gravity and trajectories:**
    - The tutorial tip for gravity is "look at the Green axis line on the gravity object".
    - "It is extremely tedious to figure out where Mario will land" for velocity launchers such as pipes, sling stars without paths, Piantas, WaterPressure and ? coins (#64).
12. **Cameras edited blind:**
    - Launch Cam Plus "requires some learning… as there is no visual feedback". The CANM editor has "no visual feedback, meaning that it is somewhat difficult to use" ([Tools page](https://www.lumasworkshop.com/wiki/Tools_and_Resources), archived).
    - Camera IDs are Japanese strings built from hex, e.g. `e:スーパースピンドライバー:005:01番目`.
13. **Scenario/layer bitmasks:**
    - ScenarioData stores, for each zone, an integer layer mask that you add up from powers of two. A forum explanation got its own example wrong: "D and J which is 2^9 and 2^3" swaps the two exponents ([SMW Central tutorial](https://www.smwcentral.net/?p=viewthread&t=76115), via search).
    - SMG1 needs a separate legacy Whitehole for its "Zone-Layer editor".
14. **Many tools for one edit:** there's a separate tool for scenarios, cameras, text, collision, models and particles (see A5). Text editing is a separate MSBT tool, and Whitehole's built-in MSBT editor is "very useful, but slightly buggy… can cause some things to not be saved" unless you edit in a set order ([Text_Editing_Tutorial](https://www.lumasworkshop.com/wiki/Text_Editing_Tutorial)).
15. **Selection and editing bugs:**
    - Typed transform values get applied to the newly selected object when you click another one in the list (#62).
    - Paste shortcuts fire while typing (#4).
    - Arrow keys in the list drop the selection (#81).
    - Sticky keys (#98).
16. **One galaxy at a time:** you can't copy between galaxies. "When a Galaxy is open, you cannot open a further one" (#55). Opening several galaxies is requested so you can recreate a setup from one galaxy in another.
17. **Codenames:** galaxies are listed by internal name (EggStarGalaxy = Good Egg, HeavensDoorGalaxy = Gateway). Despag and Neo add display names.
18. **Hard limits that crash without warning** (AreaObj_Limitations, archived): CubeCamera 160, SwitchArea/DeathArea/Water/WarpCube 64, ChangeBgmCube 32, Plane(Circular)ModeCube 16, PipeModeCube and TowerModeCylinder 4, BgmProhibitArea 4. "If the game exceeds this limit, the game will very likely crash". Also: ≤7 galaxies per world ID, ≤93 scenario files, and a planet must be registered in PlanetMapDataTable or it won't appear ([Creating_a_New_Galaxy_Tutorial](https://www.lumasworkshop.com/wiki/Creating_a_New_Galaxy_Tutorial), archived).
19. **General difficulty:** the Luma's Workshop guide intro says "Super Mario Galaxy modding is difficult and cumbersome at times" ([guides](https://lumasworkshop.com/guides/), via search snippet).

### A5. What a typical edit takes (from tutorials)

- **Kuribo64 manual, SMG1** ([thread 2702](https://kuribo64.net/board/thread.php?id=2702)):
  1. Extract the disc.
  2. Run Whitehole (Java), pick the game folder, open a galaxy by codename.
  3. Edit, then save.
  4. If you placed a planet, add a row to `PlanetMapDataTable.bcsv` in the BCSV editor.
  5. Copy the SMGTools Riivolution template XML and folder to the SD card, then copy the edited `StageData/*.arc` and `ObjectData` table across.
  6. Boot Riivolution on a Wii or in Dolphin ("Start with Riivolution Patches"). Every iteration means saving, copying the files and cold-booting the game.
- **New SMG2 galaxy** (Creating_a_New_Galaxy_Tutorial):
  1. In Dossun2, create the galaxy and zone and fill in scenario entries.
  2. Make the world ID work, which can mean deleting another galaxy's `GalaxyInfo.bcsv` in WiiExplorer or editing GalaxyWorldOrderList and GalaxyDataTable.
  3. Use Whitehole's worldmap tab: "QuickActions → Add Connected Point".
  4. Add `GalaxyName_`, `GalaxyNameShort_` and `ScenarioName_<Galaxy><N>` entries to SystemMessage.arc MSBTs with an MSBT tool.
  5. Tip from the tutorial: "If you experience any bugs when editing world maps, use the one from v.1.5.1."
- **Silver Stars, SMG2:** in Whitehole, set the CollectTico `l_id`, set each StrayTico's GeneratorID to it, and set the PowerStar `Obj_arg0` to the scenario number. Then in Scenaristar, paste the Japanese string `チコ集め` into "Power Star Trigger" ([tutorial](https://www.lumasworkshop.com/wiki/Silver_Star_Setup_Tutorial)).
- **The full toolbox** ([Tools and Resources](https://www.lumasworkshop.com/wiki/Tools_and_Resources)):
  - Level and scenario: Whitehole Neo, Scenaristar, Dossun2
  - Cameras: Launch Cam Plus, CANM editor
  - Archives and data files: WiiExplorer, RARC tools, pyjmap (BCSV↔CSV), pygapa (particles)
  - Models, animation and collision: SuperBMD / j3d tools, NeoKclCreate / pyKCLCreate (collision)
  - Text and layout: msbconv / MSBT_Editor / Tomato (text), Switch Toolbox (layouts)
  - Code and character parameters: Syati (code), SMG2MarioConstEditor
  - Slides: a Blender add-on "Slide Calculator" that converts Bezier curves to Whitehole coordinates
  - Testing: Riivolution + Dolphin
  - Engine: GLE (Galaxy Level Engine), SMG2-only Riivolution memory patches that add 8-star select, custom hubs and NoBootOut stars ([GLE](https://github.com/SuperHackio/GalaxyLevelEngine))
  - "Pyjkernax": could not verify that this exists.

---

## Part B: Mechanics worth borrowing

### Super Mario Maker 2 ([mariowiki Course Maker](https://www.mariowiki.com/Course_Maker), [Gamepur undo](https://www.gamepur.com/guides/super-mario-maker-2-undo-button))
- **One button toggles play and edit.** Minus or the clapperboard starts play from where Mario is placed; *holding* it starts from the level start. Dying during a test drops you back into edit mode.
  - Borrow as: one key or button. Tap = play from the current player marker or camera. Hold = play from the scenario's start.
- **Safety net when you start from a placed position:** if there's no ground within 2 screens, a 3-block Start Block platform appears.
  - Borrow as: when starting mid-air on a planet, spawn a temporary floor aligned to gravity, or snap Mario to the nearest surface along gravity.
- **Trail of the last test run** (toggle the Small Mario icon).
  - Borrow as: record Mario's path and jump arcs in play mode and draw them as a ghost polyline in edit mode. This also answers the trajectory request in #64.
- **Undodog:** hold B to undo, with a confirming animation and sound. The Reset Rocket (wipe everything) can itself be undone.
  - Borrow as: every destructive action is undoable, including "delete zone" and "reset layer". Give each undo a short audio and visual cue.
- **Part palettes and recently used parts**, with a sound for every placement, so feedback is felt as well as seen.

### Blender ([transform](https://docs.blender.org/manual/en/latest/scene_layout/object/editing/transform/introduction.html), [snapping](https://docs.blender.org/manual/en/3.6/editors/3dview/controls/snapping.html))
- **Modal G/R/S:** after the key, press X/Y/Z to lock an axis (press twice for the local axis), Shift+axis to exclude it, type a number to enter an exact value, then Enter/LMB to commit or Esc/RMB to cancel. Neo has only a "hold G" approximation.
  - Borrow as: add a **G** (gravity-relative) axis mode that moves along or perpendicular to the local gravity vector.
- **Snapping:** Face Project plus "Align Rotation to Target" snaps an object onto a surface and aligns its Z axis to the surface normal. "Project Individual Elements" snaps each selected object separately.
  - Borrow as: the core placement mode for spherical planets. Drop onto the collision under the cursor with up = −gravity, and drop a multi-selection object by object.
- **3D cursor:** a placement and pivot anchor you can snap to surfaces.
  - Borrow as: new objects spawn at the cursor, and it can act as the group-rotate pivot (#79 asks for this).
- **F3 operator search:** fuzzy search over every command.
  - Borrow as: one palette that searches commands, objects and the DB, so typing "goomba" or "Kuribo" both work.
- **Pie menus** (radial, opened from a key or gesture): they map well to a controller stick.
- **N-panel:** a collapsible side panel with exact transform values next to the view.

### Unity / Godot / Unreal
- **Scene tree + inspector** with drag-to-reparent and search/filter.
- **F to frame the selection, double-click in the tree to frame.** Neo uses Space.
- **Play-mode tint** ([Unity tips](https://medium.com/@stonger44/changing-the-playmode-tint-in-unity-35fa1b8d48f6)): changes made in play mode are thrown away, so the whole UI gets tinted to make the mode unmistakable.
  - Borrow as: a strong colour border and HUD badge whenever the game is live.
- **Unreal "Keep Simulation Changes" (K)** ([docs](https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/LevelEditor/InEditorTesting/KeepSimulationChanges)): select actors during play or simulate, press K, and their state is copied back to the editor level.
  - Borrow as: pause the game, nudge objects, press K to commit them into the placement data.
- **Godot remote scene tree + "Synchronize Scene Changes"** ([docs](https://docs.godotengine.org/en/stable/tutorials/scripting/debug/overview_of_debugging_tools.html)): inspect and edit the running game's nodes, and push editor edits into the running game live. Visible collision shapes and paths can be turned on at runtime.
  - Borrow as: debug-draw toggles for gravity, collision, areas, rails and cameras, usable while playing.
- **Prefabs / scenes as reusable assets:** reusable setups such as "Silver Star mission" or "Launch Star + path".
- **Gizmos:** per-type viewport handles for ranges, radii and arrows. These are Supernova's planned "Obj_arg visualizers".

### Hammer / TrenchBroom ([TrenchBroom manual](https://trenchbroom.github.io/manual/latest/))
- **Grid size on keys 1–0, with face drags snapping to the grid.** Borrow this, with a gravity-aware grid (see "Spherical worlds" below).
- **2D orthographic views next to the 3D view:** useful as a top-down "map" of a spherical planet projected along gravity.
- **Modal Clip (C) and Vertex tools; Tab cycles the locked horizontal axis; Alt-drag moves vertically.** The idea of locking movement to a plane carries over directly to moving along a planet surface versus along gravity.
- **Texture/UV lock:** in our case, "keep rotation aligned to gravity while moving".

### Figma ([select layers](https://help.figma.com/hc/en-us/articles/360040449873-Select-layers-and-objects), [smart selection](https://help.figma.com/hc/en-us/articles/360040450233-Arrange-layers-with-Smart-selection))
- **Selection depth:** a click selects the parent; double-click or Enter goes one level in; Cmd-click deep-selects; Shift+Enter or `\` goes back up. Cmd-right-click lists every layer under the cursor.
  - Borrow as: click selects the zone or group; double-click enters it. Alt-click cycles through overlapping objects, which matters because areas, gravities and objects overlap heavily.
- **Select matching** (Opt+Cmd+A): select every object of the same type or with the same switch ID.
- **Properties panel shows mixed values** for a multi-selection, and editing one value changes all of them. This replaces Neo's Group_Move panel.
- **Smart guides, Tidy up, and spacing handles:** use these to evenly space coins around a ring or along a path.
- **Quick actions (Cmd+/) and version history** with named checkpoints you can restore.

### Dreams (PS4) and LittleBigPlanet ([PS Blog](https://www.playstation.com/en-us/editorial/this-month-on-playstation/dreams-beginners-guide/), [DreamSkool](https://dreamskool.wordpress.com/2019/02/06/logic-gadgets-controller-sensor/), [LBP Create Mode](https://littlebigplanet.fandom.com/wiki/Create_Mode))
- **Imp cursor:** the controller is the pointer, via gyro or sticks, and grab-and-pull moves things in 3D.
  - Borrow as: a free-flying cursor that sticks to surfaces, with right stick to orbit and triggers for depth along gravity.
- **Gadget wires:** you drag visible wires from output ports to input ports, and a microchip groups logic. Newly stamped gadgets come with sensible default wiring.
  - Borrow as: draw SW_A/SW_B/SW_DEAD → SW_APPEAR links as animated wires between objects, and create one by dragging from a port. The editor assigns the numeric switch ID itself.
- **Tweak menu on hover:** Square over a thing opens its settings. In LBP3, R2 shows info such as glued objects and attached logic.
- **LBP rewind:** undo and redo presented as rewinding a VCR, mapped to the d-pad. L3 toggles preview, after complaints about long trips through the pause menu.
- **Possess / play anywhere:** in Dreams you jump into any character and play it instantly.

### Others
- **Roblox Studio "Play Here"** ([Fandom](https://roblox-studio.fandom.com/wiki/Test)): spawn the player at the camera; "Run" runs the simulation with no player. Godot has an addon that copies this.
- **Fortnite Creative phone tool + UEFN Live Edit** ([docs](https://dev.epicgames.com/documentation/en-us/fortnite/live-edit)): you edit while playing, in third person, with a held tool. UEFN edits flow into the running island.
- **Spore Galactic Adventures** ([Adventure Creator](https://spore.fandom.com/wiki/Adventure_Creator)): the closest precedent for editing on a planet.
  - Drag from the palette onto the sphere and the object sits on the surface. A blue ring rotates it around the local up axis, and Tab shows full 3-axis handles.
  - Ctrl-drag raises or lowers along the local up axis, Shift-drag slides along the surface, and Alt-drag duplicates.
  - A complexity meter enforces the limits. Borrow as: an AreaObj and heap budget bar.
- **Nintendo's own SMG tool** was apparently called **"MapTool"**, from `[FHISTORY]` paths left in `ObjectData/BathtubPlanet.arc/jmap.ini` ([TCRF](https://tcrf.net/Super_Mario_Galaxy), via search). No footage is known. LunchPack is the Wii U/Switch engine; no public editor footage was found beyond Nintendo recruitment videos (see [TCRF SMO prerelease](https://tcrf.net/Prerelease:Super_Mario_Odyssey)).
- **Fan editors for 3D World and Odyssey** (Spotlight, OdysseyEditor) follow the same tree + property grid + viewport pattern as Whitehole. Nothing new there for gravity.

### Spherical worlds and gravity: synthesis

No mainstream editor treats gravity as a first-class concept. Combine Spore's surface-relative drag modifiers, Blender's align-to-normal face snapping, and the game's own gravity evaluator. In Petari we can call `MR::calcGravityVector` at any point, so the editor can use the *real* gravity field rather than an approximation. That allows:
- A "gravity up" frame for the transform gizmo.
- Drop-to-ground along gravity.
- Field-line or arrow visualisation sampled on a grid around the cursor.
- Each gravity volume drawn with its range (Range+Distant) and coloured by priority.
- Highlighting which gravity wins at the cursor and which ones are being summed.

---

## Part C: SMG vocabulary for UI labels

### C1. Common placement fields
Neo's friendly label is given in brackets.

- **l_id** [Link ID]: the object's own ID, used as a target by others.
- **Obj_ID**, **MapParts_ID**, **ChildObjId**, **FollowId** [Linked …]: pointers to another object, map part, child or area.
- **GroupId** [Group ID]: groups objects together, e.g. warp pad pairs, Launch Star with TicoFat, PowerStarAppearPoint.
- **ClippingGroupId** [Clipping Group ID]: "Objects in this group are always clipped together."
- **ViewGroupId** [View Group ID]: draw/visibility group.
- **DemoGroupId** [Cutscene Group ID], **CastId** [Cast Group ID]: cutscene membership.
- **CommonPath_ID** [Path ID]: rail index in the zone. Rail objects require it, and a Launch Star without one crashes.
- **ShapeModelNo** [Model ID]: model variant.
- **CameraSetId** [Camera Set ID]: builds event and start camera IDs.
- **MessageId** [Message ID]: picks the MSBT label `<Obj>%03d`.
- **ParamScale** [Speed Scale]: applied while SW_PARAM is on.
- **GeneratorID** (SMG2) / **ParentID** (SMG1) [Generator Object ID]: links a child to its controller's l_id.
- **AreaShapeNo** [Shape ID]: area shape (DB shapes: BaseOriginCube, CenterOriginCube, Sphere, Cylinder, Bowl).
- **Priority**: areas and gravity.
- **Path_arg0–7** (per path), **Point_arg0–7** (per point), **SignMotionType**, **MoveConditionType**, **Rotate*** fields (RotateMoveObj).
- **Placement lists** (BCSV tables in the zone arc, with object counts across the DB): ObjInfo 1843, MapPartsInfo 406, AreaObjInfo 160, PlanetObjInfo 15 (gravity), ChildObjInfo 10, CameraCubeInfo 8, DemoObjInfo, StartInfo, SoundInfo; also StageObjInfo for zones.

### C2. Switches (verified in decomp, `StageSwitch.cpp`)
- **ID ranges:** −1 = none. **0–127** are zone-local, one 128-bit `BitFlag128` per zone. **1000–1127** are galaxy-global: `mIsGlobal = id >= 1000`, and index = id − 1000. Any other value has no valid slot, so the editor should block it.
- **SW_APPEAR** (read): the object stays hidden until the switch is on.
- **SW_DEAD** (write): set when the object dies or is collected. In the DB, 65 classes use it for "Gets activated when it dies", 60 for "…killed through a cutscene action".
- **SW_A / SW_B** (read or write, depending on the object):
  - Coin: SW_B kills the coin.
  - SuperSpinDriver: SW_A = shrunk and unusable until the switch is on; SW_B = disappears while it's on.
  - BlueChipGroup: SW_A is written when all chips are collected.
  - SwitchArea: writes SW_A on entry, with SW_B as an enable gate.
  - Gravity: SW_A = active while the switch is on; SW_B = active while it's off.
  - CameraArea: SW_A enables; SW_B blocks.
- **SW_AWAKE** (read): "Enables rendering and resumes updates when this switch is active" (399 classes).
- **SW_SLEEP**: the opposite of SW_AWAKE. The DB doesn't have it yet (Neo #108).
- **SW_PARAM** (52 classes): "path movement speed is adjusted with ParamScale while this switch is activated".
- **SMG2 `ActorInfo/InitSwitch.bcsv`** declares each actor's switches as UseRead / UseWrite / UseWriteAuto, plus Need* variants for required switches ([ActorInfo](https://www.lumasworkshop.com/wiki/ActorInfo), via search). This is ideal data for drawing typed ports in a wire UI.

### C3. Obj_args for common objects (galaxydatabase)
Booleans follow the "−1 = default/off" convention.

- **Coin:**
  - arg3 Shadow Calculation (bool)
  - arg4 Pose Type: −1 rotation and shadow follow gravity; 0 rotation ignores gravity; 1 attach to the collision below (SMG2)
  - arg5 Shadow Length (default 1000)
  - arg6 Shadow Type: −1 sphere, 0 circle, 1 cylinder
  - arg7 Inside Bubble?
  - Switches: SW_APPEAR, SW_B (kill), SW_AWAKE
- **Kuribo (Goomba):**
  - arg0 Don't Calculate Gravity? (uses its down vector)
  - arg1 Initial State: −1 idle; 0/1 appear forwards/backwards from a box
  - Switches: SW_APPEAR, SW_DEAD, SW_AWAKE
- **SuperSpinDriver (Launch Star):**
  - arg0 Flight Time (default 300)
  - arg1 Shadow Length
  - arg2 Player Pull Type: −1 pulled in, 0 not
  - arg3 Camera Count (event cams)
  - arg4 Appearance Jingle: −1 play, 1 silent
  - arg5 Player Rotation
  - arg6 Respawn Type
  - arg7 Draw Path Range Index
  - Path_arg1 Visible Path Cut Length
  - Requires a Rail. Message label `SuperSpinDriver%03d`.
- **Poihana (Cataquack):**
  - arg0 Launch Intensity (default 1000)
  - arg3 Behavior: −1 walk; 1 sleep; 2 walk and reset home
  - arg4 Can Drown?
  - arg7 Use Small Binder?
  - Has a Camera (used when flinging)
- **PowerStar:**
  - arg0 **Power Star ID** = the scenario number it belongs to
  - arg1 Appearance Gravity?
  - arg2 Ignores Gravity?
  - arg3 Elliptic Cylinder Shadow?
  - arg4 Appear at Nearest Point? (PowerStarAppearPoint group)
  - arg5 Shadow Length
  - LinkNamePos `スターゲットデモ座標` (star-get demo position)
- **BlueChip / BlueChipGroup:**
  - BlueChip: arg0 Group ID, which must match the group; arg3 In Bubble; arg4 Shadow Length (2000); arg5 Shadow Type; can ride a rail via Path_arg4 and Point_arg0/1/5/7.
  - Group: arg0 Group ID; arg1 Time Limit (−1 none); arg2 Counter Display Range (2000); arg3 Reactivation Type; SW_A is written when all chips are collected (recommended: 5 chips).
- **TicoShop (Lumalee):**
  - arg0 Requested Star Bits (default 30)
  - arg7 Special Talking Camera? (uses camera `独自会話`)
  - SW_A/SW_B are set by event-flow nodes
  - EventFunc 0–4 (eat bits, show/hide counter, chose Life or 1-Up mushroom); BranchFunc 0 (has enough Star Bits?)
- **Luma (Tico)**, from the K64 tutorial: arg0 colour: 0 yellow, 1 blue, 2 green, 3 red, 4 purple (unused), 5 pink, 6 orange (SMG2).
- **Silver Stars:** CollectTico arg0 "Combine at Controller's Position?", writes SW_A and AppearPowerStar. StrayTico writes SW_A when collected. Also needs the scenario's PowerStarAppear set to `チコ集め` (SMG2).

### C4. Gravity

**Common fields** (verified in decomp, `GravityUtil.cpp` and `PlanetGravity.cpp`):
- **Range** (float): −1 means infinite. A point is in range if its distance < Range + Distant.
- **Distant** (float): added to Range, and also subtracted from the radius for point gravity.
- **Priority** (int): gravities are kept sorted in descending order. The highest priority in range wins, and **gravities with equal priority have their vectors summed**.
- **Inverse** (bool): flips the gravity.
- **Power** (string): `Light`, `Normal` or `Heavy`.
- **Gravity_type** (string): `Normal`, `Shadow` (affects only drop shadows) or `Magnet`.
- **Gravity_id**.
- Switches SW_A, SW_B and SW_AWAKE.
- Stored in **PlanetObjInfo**. Each zone layer has its own list.

**Types:**

| Object | Class | Notes and args |
|---|---|---|
| GlobalPointGravity | PointGravity | Sphere; scale ignored |
| GlobalPlaneGravity | ParallelGravity | Planar with a sphere range (SMG1 only) |
| GlobalPlaneGravityInBox | ParallelGravity | arg0 base distance; arg1 distance calc axis |
| GlobalPlaneGravityInCylinder | ParallelGravity | arg0 base distance |
| GlobalDiskGravity | DiskGravity | arg0 disable bottom; arg1 disable edges; arg2 valid degrees (360) |
| GlobalDiskTorusGravity | DiskTorusGravity | arg0 both sides; arg1 edge type; arg2 disk radius |
| GlobalCubeGravity | CubeGravity | arg0/1/2: valid faces on X/Y/Z (−1 both, 0 none, 1 + side, 2 − side); scale = cube size |
| GlobalConeGravity | ConeGravity | arg0 disable bottom; arg1 top cut length (SMG1 only per wiki) |
| GlobalSegmentGravity | SegmentGravity | "Cylindrical gravity". arg0 enabled bases (−1 both, 0 none, 1 bottom, 2 top); arg1 valid degrees |
| GlobalWireGravity | WireGravity | arg0 point count (default 20); requires a Rail |
| GlobalBarrelGravity | BarrelGravity | SMG2; 2D barrel passages |
| ZeroGravitySphere / Box / Cylinder | ParallelGravity | SMG2; no gravity, objects keep their own up |
| MarioLauncherAttractor | | Listed in the gravity category |

- **There is no "GlobalCylinderGravity".** Cylinder gravity is GlobalSegmentGravity; GlobalPlaneGravityInCylinder is a planar gravity with a cylinder range.
- Wiki illustrations exist for point, cube, disk, torus, cone, segment, wire and ParallelInBox (Gravity page, archived).

### C5. Cameras

Source: the Luma's Workshop [Cameras](https://www.lumasworkshop.com/wiki/Cameras) page (archived) and the DB.

- **Storage:** a zone's `Stage/camera/CameraParam.bcam` (BCSV), keyed by an `id` string.
- **ID families:**
  - **Cube:** `c:%04x` from a CameraArea's Obj_arg0. Hex must be lowercase, e.g. arg0 = 15 → `c:000f`.
  - **Start:** `s:%04x` from the StartInfo entry's CameraSetId.
  - **Event:** `e:<JP event name>:<setID hex>:<idx>番目`, e.g. `e:スーパースピンドライバー:005:01番目`.
  - **Group:** `g:<name>`.
  - **Other:** `o:デフォルトカメラ` (default), plus default underwater, water-surface, flying (フーファイター) and zoom (first-person) cameras.
- **Camera areas:**
  - SMG2 uses `CameraArea`. SMG1 uses `CubeCameraBox`, `CubeCameraCylinder`, `CubeCameraSphere` and `CubeCameraBowl`, all class CubeCameraArea in list CameraCubeInfo.
  - Args: arg0 Camera ID; arg2 Priority (highest wins); arg3 Affected Contexts bitfield: −1 all, 1 normal, 2 underwater, 4 water surface, 8 Pull Star, 16 flying, 32 Yoshi, 64 Bulb Yoshi.
  - Limit: 160 CubeCamera instances.
- **camtype values (selection):** CAM_TYPE_XZ_PARA (parallel; dist, angleA, angleB), WONDER_PLANET, TOWER, TOWER_POS, INWARD_TOWER, INWARD_SPHERE, POINT_FIX, EYEPOS_FIX, EYEPOS_FIX_THERE, TRUNDLE, SPHERE_TRUNDLE, INNER_CYLINDER, CUBE_PLANET, CHARMED_FIX, CHARMED_VECREG(_TOWER), MEDIAN_PLANET, MEDIAN_TOWER, FOLLOW, RACE_FOLLOW, WATER_FOLLOW, WATER_PLANET, and more.
- **Common BCAM fields:**
  - Angles and distance: angleA/angleB (radians), dist, fovy (45), roll
  - Points and axes: wpoint, axis, up, woffset, vpanaxis
  - Timing: camint (interpolation frames), udown, pushdelay, gndint
  - Play-area limits: lplay/uplay, lower/upper
  - Flags: flag.noreset, nofovy, antibluroff, collisionoff, subjectiveoff, …
  - Event-only: evfrm, evpriority, eflag.enableErpFrame, camendint

### C6. Scenarios, zones and layers
- **Scenario archive:** `<Galaxy>Scenario.arc` contains `ScenarioData.bcsv`, with one row per scenario (star). It also holds ZoneList and GalaxyInfo.
- **Zone columns:** for each zone, an int **layer mask**: bit0 = LayerA (1), bit1 = B (2), C = 4, D = 8 … P = 2^15 = 32768. `Common` is always loaded, so mask 0 means Common only. Column names are hashed, which is why BCSV editors show hex like `D6C80400`. ([search summary of SMW Central and Scenaristar](https://github.com/SuperHackio/Scenaristar))
- **Placement storage:** each zone's map arc stores placement per layer (`Placement/Common`, `Placement/LayerA` … `LayerP`), each holding ObjInfo, MapPartsInfo, AreaObjInfo, PlanetObjInfo, CameraCubeInfo, StartInfo, etc.
- **Zone placement:** zones are placed in the galaxy's main stage via StageObjInfo, with a position and rotation that move the whole zone.
- **Other scenario fields:** star type and comet type, time limits, and the PowerStarAppear trigger (e.g. `チコ集め` for Silver Stars). Comets and green stars are gated by game flags.
- **SMG2 world IDs:** at most 7 galaxies per world; at most 93 scenario files.

### C7. Object database and licence
- **Source:** [SunakazeKun/galaxydatabase](https://github.com/SunakazeKun/galaxydatabase), archived 2024-02-24 and continued at [SMGCommunity/galaxydatabase](https://github.com/SMGCommunity/galaxydatabase) (updated Sep 2026). Browsable at [sunakazekun.github.io/galaxydatabase](https://sunakazekun.github.io/galaxydatabase/).
- **Format:** `objectdb.json` (canonical, edited through a PyQt editor) plus a generated `objectdb.xml` for Whitehole via `genwhitehole.py`.
  - **Objects:** InternalName, ClassNameSMG1/2, display Name, Notes, Category, AreaShape, ListSMG1/2, File, Games, Progress, IsUnused, IsLeftover.
  - **Classes:** Parameters keyed by field, each with Name, Type (Integer/Float/Boolean/Bitfield), Games, Needed, Description, Values[{Value, Notes}] and Exclusives.
  - **Categories:** stagepart, levelfeature, background, enemy, boss, npc, item, particle, areas, gravity, cutscene, controller, deprecated.
- **Coverage:** all 2,446 objects have Progress 2. Classes: 672 complete, 3 partial, **225 undocumented**. 192 objects are flagged unused and 57 leftover.
- **Licence:** **neither repo has a LICENSE file or licence metadata**, so by default all rights are reserved. Bundling the data needs permission from the maintainers (Aurum/SunakazeKun, SMGCommunity). Supernova is MIT but says it *sources* tooltips from the DB, so check how it handles this. Whitehole Neo also has no LICENSE.
- **Older DB:** `kuribo64.net/objectdb/smg.php?game=2` (`objectdb.xml` for Whitehole 1.6/1.7).

---

## Design implications for Petari (brief)
1. **Hot reload is the key feature:** edit inside the running game, with no rebuild, no Riivolution and no "Disc Not Read". This removes the biggest loop cost in the community.
2. **Validate before running:** catch missing rails, missing MessageId text, AreaObj limits, switch ID ranges and required fields as live lint, not as a warning when you close the galaxy.
3. **Hide numeric linking:** show switches, groups, l_id/GeneratorID and camera IDs as wires and pickers, and let the editor allocate the numbers.
4. **Gravity-aware placement and gizmos,** driven by the real gravity code.
5. **Show scenario × zone × layer as a matrix of checkboxes,** never a bitmask. Add a "which layers is this object in" badge.
6. **Undo, redo and autosave from day one,** and write to a separate project folder, never over retail files.
