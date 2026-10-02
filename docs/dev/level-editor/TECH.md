# Petari Level Editor: technical design

Companion to [UX.md](UX.md) (the interaction design). This document covers what a
Galaxy stage is on disc, how the editor changes a running game, how it draws
and picks, which UI technology to use, how edits are saved and shared, how new
terrain fits in, and the milestone plan. The feasibility spike in section 8 ran
in the real game; its numbers are quoted throughout.

Paths are relative to the repository root. `file:line` references were checked
against `main` at 66aa02302.

## 0. Summary

| Decision | Choice | Why (short) |
| --- | --- | --- |
| Source of truth | An **EditModel**: the stage's BCSV tables parsed once, plus editor-only data. Every edit is an operation on it | Undo, diff, save and patch all fall out of one model. The live scene is *derived* from it |
| Live edits | **SceneSync** re-creates the actors of changed rows in the running scene (0.1-0.5 ms each), or patches them in place while dragging. Anything it cannot do live becomes *pending* and is applied by a **reload from the model** | The spike proved runtime creation from synthesized rows, including archives loaded on demand (1-2 ms) |
| Reload | Same-stage restart through the game's own miss path, fed by edited archives in memory. 3.9 s measured today, target about 2 s | The edit survives because stage archives sit in the file cache across a same-stage reload |
| Simulation | Edit = a GameScene nerve like photo mode (movement stops; calcAnim and draw continue). Play = reload when dirty, then unfreeze. Return to Edit = freeze only | Matches the UX "Tab" loop; one reload per cycle, on the Play side |
| Threads | All game mutation on the game thread at the frame seam; the UI talks to it through a command queue and reads a snapshot | The ImGui overlay is drawn after the game thread releases the CPU baton |
| Rendering | Editor 3D overlays in GX (TDDraw, already running on Aurora) at the end of `draw3D`, depth-tested; 2D labels in ImGui | Wires and gravity shapes must sit in the world, occluded correctly |
| Picking | CPU: ray against actor bounds, then model shape boxes, plus a map-collision ray | No ID buffer needed; all inputs already exist |
| UI | **Dear ImGui, docking branch** (already compiled into Aurora), themed with the game's fonts. Small AppKit bridges for file dialogs, the menu bar and gestures | Lowest effort and the same look as the Home menu. Native panels would mean a second UI stack, a second input path and a threading bridge |
| Sharing | Default: a **patch mod** (our row operations, plus user-made assets), turned into archives on the player's machine from their own disc. Option: full archives, as Whitehole writes | No Nintendo data in shared mods. Full archives keep Whitehole and Dolphin compatibility |
| Terrain | One pipeline: brushes and imports become a project mesh. On build: **KCL + .pa + BDL generated on the user's machine**, registered as a new planet through PlanetMapDataTable. Live blockout uses in-memory KCL plus an editor preview draw | The game's own collision and planet code, portable to Wii/Dolphin; materials cloned from the user's disc so nothing copyrighted ships |
| New galaxies | A native **mod galaxy registry** (`galaxy.json`) supplies what the executable hard-codes: GalaxyID rows, event flags, names, borrowed music. Save progress goes to a per-mod sidecar | Today a mod galaxy with a Power Star crashes (null event flag, missing message) and more than 2 new galaxies overflow a fixed array (5.7) |

The riskiest assumption ("we can create an actor at runtime from a synthesized
placement row in a running stage") is **proven**. See section 8.

---

## 1. Data model and formats

### 1.1 What a galaxy is on disc (SMG1, RMGE01)

SMG1 has **one archive per zone**. The Map/Design/Sound split is Super Mario
Galaxy 2's layout, not this game's. Under `files/StageData/`:

| File | Contents |
| --- | --- |
| `<Zone>.arc` | One per galaxy and per zone (`EggStarGalaxy.arc`, `SoramameZone.arc`). A Yaz0-compressed RARC with root folder `stage`: `jmp/` placement tables, `camera/CameraParam.bcam`, `camera/StartScenario<N>.canm` (intro cameras), and optional nested archives under `/arc` |
| `<Galaxy>/<Galaxy>Scenario.arc` | `ScenarioData.bcsv` (missions) and `ZoneList.bcsv` (the zones the galaxy loads) |
| `ObjNameTable.arc` | `en_name` to `jp_name`, the internal (Japanese) class names the factory's creators receive |

How they load:
- `StageFileLoader::makeStageArchiveNameList` (`src/Game/Scene/StageFileLoader.cpp:28-40`) mounts `/StageData/<zone>.arc` for **every zone in ZoneList**, asynchronously (`:14-18`). It also mounts archives nested under `/arc` (`:46-70`).
- Yaz0 is detected by its header, not by file extension (`FileRipper::checkCompressed`, `src/Game/System/FileRipper.cpp:23-35`), so plain RARCs load too. The mod folder relies on this (native/MODS.md).
- Object models are found in `/ObjectData/<name>.arc`, then `/MapPartsData/`, then by the bare path (`src/Game/Util/FileUtil.cpp:195-211`).

### 1.2 JMap / BCSV

Every placement table, the camera file and most system tables are BCSV
(`include/Game/Util/JMapInfo.hpp`):

- **Header:** `{numEntries, numFields, dataOffset, entrySize}`.
- **Field records** (12 bytes each): `{hash, mask, offset, shift, type}`.
- **Then:** the rows, then a **string pool**.
- **Types:**

  | Type | Meaning |
  | --- | --- |
  | 0 | long (masked and shifted) |
  | 1 | inline 32-byte string |
  | 2 | float |
  | 3 | long |
  | 4 | short |
  | 5 | byte |
  | 6 | string offset into the pool |
  | 7 | null |

- **Field names are hashed** (`h = c + 31*h` over signed chars, `src/JSystem/JGadget/hashcode.cpp:7-19`). Lookup is a linear scan by hash (`JMapInfo::searchItemInfo`, `src/Game/Util/JMapInfo.cpp:29-44`).
- **Columns are optional:** a missing field reads as "absent", and the `*WithInit` helpers default it to -1 (`src/Game/Util/JMapUtil.cpp:181-189`). So rows from different tools can carry different column sets, and the editor must keep unknown columns.
- **Names are hashes on disc**, so the editor needs a dictionary of known field names to show them. Unknown hashes are shown as `#8c657583` and kept as-is.
- **Some tables are searched by value with `findElement`**, which is linear: for example `l_id` in `CommonPathInfo`, and `en_name` in ObjNameTable. The game binary-searches only where it sorts first (camera chunks, see 2.9). Row order otherwise matters only for placement order inside a `(name, ShapeModelNo)` group.

### 1.3 The placement tables

- `StageDataHolder::attachJmpInfoToArray` (`src/Game/Scene/StageDataHolder.cpp:542-559`) attaches **every file** in a directory. The table name is the file name, so the list below is a convention, not a hard-coded set.
- **Directories read:**
  - `jmp/Placement/<Layer>`, `jmp/MapParts/<Layer>`, `jmp/Start/<Layer>`
  - `jmp/GeneralPos/<Layer>` and `jmp/ChildObj/<Layer>`, if present
  - `jmp/List`
  - `jmp/Path`
- `jmp/Debug` is never read.

| Table | What it places | Notes |
| --- | --- | --- |
| `ObjInfo` | Objects, enemies, NPCs, items | The bulk of a level |
| `MapPartsInfo` | Moving and rotating map parts | Same creators; has `MoveConditionType`, `RotateSpeed` and similar |
| `PlanetObjInfo` | Gravity fields (`GlobalPointGravity`, `GlobalCubeGravity`...) | **Priority** table: placed before normal objects (`src/Game/Scene/StageDataHolder.cpp:34-37`) |
| `AreaObjInfo` | Invisible volumes (`SwitchCube`, `DeathCube`, `ChangeBgmCube`...) | Priority. Has `FollowId` and `ChildObjId` |
| `CameraCubeInfo` | Camera areas | Priority. Their camera lives in the `.bcam` |
| `DemoObjInfo` | Cutscene (demo) controllers | Priority |
| `StageObjInfo` | **Zones** placed in this galaxy (name, pos, dir; no scale) | Read at init to build child `StageDataHolder`s (`:690-705`) |
| `StartInfo` | Mario starts (`MarioNo`, `Camera_id`) | `:444`, `:227` |
| `GeneralPosInfo` | Named points (`PosName`) used by code | `src/Game/Util/SceneUtil.cpp:221-228` |
| `ChildObjInfo` | Children of an object (`ParentID` = parent's `l_id`) | `src/Game/Scene/StageDataHolder.cpp:290-346` |
| `SoundInfo` | Sound emitters | Normal placement path |
| `List/StageInfo`, `List/ChangeSceneListInfo` | Present on disc; no live reader found | Keep as-is |
| `Path/CommonPathInfo`, `Path/CommonPathPointInfo.<n>` | Rails | 1.5 |

The `ObjInfo` schema on Good Egg (dumped):
- `name` (string pool), `l_id`, `Obj_arg0..7`, `CameraSetId`
- `SW_APPEAR`, `SW_DEAD`, `SW_A`, `SW_B`, `SW_SLEEP`, `MessageId`
- `pos_*`, `dir_*`, `scale_*`
- `CastId`, `ViewGroupId`, `ShapeModelNo`, `CommonPath_ID`, `ClippingGroupId`, `GroupId`, `DemoGroupId`, `MapParts_ID`

A row's class is read from `type`, falling back to `name` (`MR::getObjectName`, `src/Game/Util/JMapUtil.cpp:66-76`).

### 1.4 Layers, scenarios and zones

- **Layers.** Every table directory has up to 17 layers: `Common` and `LayerA`..`LayerP` (`src/Game/Scene/StageDataHolder.cpp:31-32`). Common is always on.
- **Scenarios.** `ScenarioData.bcsv` has one row per mission and **one int column per zone**, named after the zone, holding that zone's layer bitmask for the mission (bit 0 = LayerA; `ScenarioDataParser::getCurrentScenarioLayers`, `src/Game/System/ScenarioDataParser.cpp:217-231`).
  - Other columns: `ScenarioNo`, `ScenarioName`, `PowerStarId`, `AppearPowerStarObj`, `Comet`, `LuigiModeTimer`, `IsHidden`, `ErrorCheck`.
- **Only the active layers are placed.** Objects of other missions do not exist at runtime, so the UX "ghosts" are drawn from the EditModel, not from live actors.
- **Zones.** Every ZoneList archive is loaded, but a zone is *placed* only where an active-layer `StageObjInfo` row names it.
  - `calcPlacementMtx` builds the zone transform from pos and dir (`:707-719`).
  - `getJMapInfoTrans` / `getJMapInfoRotate` apply it to every row of that zone when the stage has child zones (`src/Game/Util/JMapUtil.cpp:206-245`).
  - Zones are one level deep in practice (`getStageDataHolderFromZoneId` searches direct children, `:371-386`). The holder has room for 0x18 child zones.
- **Which zone a row belongs to is decided by its memory address.** `findPlacedStageDataHolder` (`src/Game/Scene/StageDataHolder.cpp:348-369`) checks the row's address against each zone's `[min, max)` table address range (`calcDataAddress`, `:657-688`). Any row the editor creates outside a loaded archive needs a native lookup hook (8.2).

### 1.5 Rails

- **Identity.** An object's `CommonPath_ID` matches an `l_id` in `CommonPathInfo`. The *row index* of that match selects `CommonPathPointInfo.<index>` (`src/Game/Scene/StageDataHolder.cpp:180-192`, `src/Game/Util/SceneUtil.cpp:287-291`).
  - Rails are **not layered** (one `jmp/Path` per zone).
  - `Path_ID` chains rails (`src/Game/Util/JMapUtil.cpp:640-642`).
- **Point rows.** Point plus two Bézier handles (`pnt0/1/2_xyz`), `point_arg0..7`, `id`.
- **Rail flags.** `closed` = `CLOSE` loops; `usage` = General or Camera.
- **Consequence for the editor.** Deleting or inserting a rail renumbers the point tables. The EditModel stores rails as objects and regenerates the `CommonPathPointInfo.<n>` files and their order on save.
- **Runtime.** `RailRider` copies the rail into a `BezierRail` at construction (`src/Game/LiveActor/RailRider.cpp:13-26`, `:205`). Editing a rail therefore means re-creating every actor that rides it.

### 1.6 Cameras

- **The file.** `camera/CameraParam.bcam` is itself a BCSV (`DotCamReaderInBin`, `src/Game/Camera/DotCamParams.cpp:47-52`). Rows match "chunks" by an `id` string:

  | `id` prefix | Source |
  | --- | --- |
  | `c:%04x` | CubeCamera area |
  | `s:%04x` | StartInfo `Camera_id` |
  | `g:%s:%d:%d` | Group camera |
  | `o:` | Other camera |
  | `e:` | Event camera |

  See `src/Game/Camera/CameraParamChunkID.cpp:69-122` and `src/Game/Camera/GameCameraCreator.cpp:33-87`.
- **Only referenced chunks are loaded.** The game creates chunks from what is placed, then fills them from every zone's `.bcam` (`CameraParamChunkHolder::loadCameraParameters`, `src/Game/Camera/CameraParamChunkHolder.cpp:78-160`). A `.bcam` row nobody references is ignored, so the editor must create the area or start and the `.bcam` row together.
- **Lookup order.** Chunks are sorted once, for binary search (`src/Game/Camera/CameraDirector.cpp:402-413`). A chunk added later needs `sort()` called again.
- **Field names.** `src/Game/Camera/CameraParamChunk.cpp:145-292` (the editor's camera inspector schema).
- **Intro cameras.** `StartScenario<N>.canm` (`src/Game/Util/SceneUtil.cpp:342-352`), a keyframe format. Editing them is out of scope for v1; keep them byte-for-byte.

### 1.7 Classes, archives and system tables

- **Factory.** `NameObjFactory::getCreator` (`src/Game/NameObj/NameObjFactory.cpp:8103-8116`) checks the order below. A name with no creator is skipped silently, which is how zone names in `StageObjInfo` pass through.
  1. **PlanetMapCreator** first: any name listed in `/ObjectData/PlanetMapDataTable.arc` becomes a planet.
  2. `cCreateTable` next (`:46`, about 1,180 entries of `{name, creator, archive}`).
- **Archives an object needs.** `getMountObjectArchiveList` (`:8169-8193`) unions three sources, collected in `NameObjArchiveListCollector` (at most 32 names):
  1. The create-table archive.
  2. `cName2ArchiveNamesTable` (`:5963`, 441 entries).
  3. 91 per-class `makeArchiveList` functions (`:7729`).

  For planets, `PlanetMapCreator::makeArchiveListPlanet` adds the `Low`, `Middle`, `Bloom`, `Water` and `Indirect` variants (`src/Game/Map/PlanetMapCreator.cpp:286-318`).
- **PlanetMapDataTable.** Columns: `PlanetName`, the five variant flags, and `ForceLowScenarioName0-7` (`"<Stage>_<scenario>"`).
- **Lighting.** Per-zone tables `Light<Zone>.bcsv` in `/ObjectData/LightData.arc` (`src/Game/Map/LightFunction.cpp:38-55`).
- **Galaxy order and metadata.** `GalaxyID.bcsv` and `StoryEvent.bcsv` are **embedded in the executable** (`src/Game/System/GalaxyNameSortTable.cpp:4-17`, `BCSVInclude_native.S:13-16`). See 5.7 for new galaxies.

### 1.8 What `native/tools/stage_edit.py` does today

The tool is a good base and already in CI (`native_stage_edit`), but it is a
CLI for row edits, not a writer for new content.

| Capability | State |
| --- | --- |
| Yaz0 decode / encode | Decode yes. Encode: greedy LZ77, 4 KiB window, valid but not optimal |
| RARC read | Yes (paths lowercased) |
| RARC write | **Rewrites offsets and sizes only**: cannot add, remove or rename files or folders |
| BCSV read / write | Types 0-6 with masks; strings re-pooled (Shift-JIS) and padded with `@`. Unedited archives rebuild **byte for byte** (tested on every disc archive) |
| BCSV structure | **Cannot add columns, delete rows or create tables** |
| Row ops | move, set field, copy row, new row (from row #0, known fields reset), fresh `l_id` = max+1 over one archive |

The editor needs a full RARC writer, BCSV column, row and table creation, and
rail renumbering (5.2). The Python tool stays the test oracle: the C++ writers
must produce byte-identical archives for unedited input, the property the Python
tests already check.

### 1.9 The EditModel

The editor's in-memory model of a galaxy:

- **GalaxyModel**: the stage name, ScenarioData rows, ZoneList, and one ZoneModel per zone archive.
- **ZoneModel**:
  - The archive's file list (unknown files kept as opaque bytes).
  - Its tables (`TableModel`: an ordered column list with hash, type, mask and shift, plus rows of typed cells, unknown columns included).
  - Rails as objects.
  - The `.bcam` chunk table.
- **Row identity.** Each row gets a **stable editor id**, assigned when the original archive is parsed: `(zone, layer, table, original index)` for original rows, a fresh UUID for new rows. `l_id` is game data, not identity: it is not unique across tables and is sometimes duplicated on disc. The patch format (5.3) and undo key off this id.
- **Editor-only data**: switch names, labels, locks, brushes and bookmarks, saved in the project, never in game files (UX section 17).

---

## 2. Live editing architecture

```
 UI (ImGui panels, gizmos)                      game thread (frame seam)
 ──────────────────────────                     ─────────────────────────────────
  user gesture ──► Command ──► queue ─────────►  EditModel.apply(op)  ── undo stack, journal
                                                     │ dirty rows / tables
                                                     ▼
                                                 SceneSync
                                                  ├─ live: re-create actor, poke transform,
                                                  │        gravity/area in place
                                                  └─ pending: mark; applied by Reload
  snapshot (selection, actors, bounds) ◄──────── publish snapshot (before baton release)
```

### 2.1 The rule

**The row is the truth; the actor is a cache.** Every edit is an operation on the
EditModel. SceneSync then brings the running scene in line with it, as cheaply as
it can:

| Change | Live strategy | Cost (measured in the spike unless marked) |
| --- | --- | --- |
| Drag a transform (preview) | Write `mPosition` / `mRotation` / `mScale` on the live actor; frozen actors re-render because calcAnim still runs | per frame, ~0 |
| Commit a transform, or change any `Obj_arg`, `SW_*`, link, `ShapeModelNo` or name | **Re-create**: kill the old actor (tombstone), create a new one from the edited row | 0.06-0.5 ms per actor |
| Add an object | Create from a new row; mount its archives first if needed | 0.1-0.5 ms; +1.3-1.8 ms if its archive was not resident |
| Delete an object | Kill + hide + invalidate clipping (a tombstone; nothing is freed) | ~0 |
| Gravity: shape, range or power | Mutate the existing `PlanetGravity` and call `updateIdentityMtx` (estimate) | ~0 |
| Gravity: add | Create through the normal path; counts against the cap of 128 (no unregister) | ~0.3 ms (estimate) |
| Area: shape, args | Re-create; the old one is invalidated | as objects |
| Rail points | Re-create every actor riding the rail | as objects |
| Zone add or move, `StageObjInfo`, start points, `.bcam`, lighting, scenario masks, row order | **Pending** → Reload | ~2-4 s |

Re-creating instead of mutating sidesteps per-class state: an actor reads its
row in `init` and the spike shows `init` works mid-game. Mutating in place is
reserved for transform previews and the few systems that have no "remove"
(gravity, areas).

### 2.2 Creating an actor mid-game

The spike's procedure (`src/Game/Scene/EditorSpike.cpp` in the spike patch), on the game thread inside `GameScene::update`:

1. **Build the row.** Build a one-row BCSV from the source table's schema and the edited values: copy the header and field records, write the row, then a fresh string pool. Wrap it in a `JMapInfo`.
   - Both live **as long as the actor**: actors keep `JMapInfoIter`s and derived ids.
2. **Register the row with its zone** so `findPlacedStageDataHolder` answers that zone for its address (hook 1, section 8.2). Positions are written zone-local.
3. **Mount the archives** if needed: `NameObjFactory::requestMountObjectArchives` then `MR::receiveAllRequestedFile`. This takes 1-2 ms on SSD.
4. **Create and initialise** the actor:
   - `setCurrentPlacementZoneId(zone)` (collision parts take their zone from it; `src/Game/Map/CollisionParts.cpp:54-73`)
   - `startInitLiveActorSystemInfo`
   - `creator(MR::getJapaneseObjectName(name))`
   - `init(iter)`
   - `initLiveActorSystemInfo(iter)`
   - `initAfterPlacement()`
   - `clearCurrentPlacementZoneId`
5. **Late connect** (hook 2). `allocateDrawBufferActorList` sizes each movement, calcAnim and draw category list, and each per-model draw buffer, to **exactly the load-time count** (`src/Game/NameObj/NameObjCategoryList.cpp:48-61`, `src/Game/System/DrawBufferExecuter.cpp:15-22`). After load nothing new is ever connected. Late connect does three things:
   - grows the arrays that would overflow
   - allocates the lists of draw executors created after load
   - calls `initConnectting()` for execute entries registered since the snapshot, as load does
6. **Clipping view groups** (hook 3): re-run `ViewGroupCtrl::initViewGroup` over the four clipping lists, so the new `ClippingActorInfo` gets its group pointer (otherwise `ClippingActorHolder::movement` dereferences null).

In production these hooks become a small `EditorRuntime` API in native code, with:
- headroom pre-reserved at load when the editor is on (about 1.5x plus 16 per category, so most adds never reallocate)
- growth as the fallback

### 2.3 Deleting, and why nothing is freed

- **The game never deletes an actor mid-scene.** `NameObjHolder` has no remove (`src/Game/NameObj/NameObjHolder.cpp:53-56`).
- **Pointers outlive the kill.** Pointers stay in the execute holder, `AllLiveActorGroup`, clipping, shadows, the gravity manager, switch listeners, demo casts and others (section 2.9 lists the capacities).
- **Delete therefore means:** `makeActorDead()`, `MR::invalidateClipping`, and hiding the model. For gravity, `mAppeared = false`; for areas, `invalidate()`.
- **The dead actor stays as a tombstone** until the next reload.
- **Memory:** the scene heaps are `JKRSolidHeap`s that take all remaining memory and free only at scene end (`src/Game/System/HeapMemoryWatcher.cpp:172-175`).
  - All game `new` during play allocates from the current (scene) heap (`src/JSystem/JKernel/JKRHeap.cpp:323-341`). Natively MEM1/MEM2 are 128/256 MB (`native/memory/arena.cpp:13-14`). Good Egg leaves about 109 MB of the scene NAPA heap free.
  - Re-creating is therefore safe for a long session, but it does creep. The editor shows it (the UX memory meter), and Play's reload resets it.
  - The meter is O(1): the solid heap's free size.

### 2.4 Capacities the editor must count

Most of these are **unchecked**: overflowing them writes past the end. The editor
tracks each one and refuses (with a Check) before the limit.

| Structure | Capacity | Where |
| --- | --- | --- |
| Scene NameObjs | 4,864 | `src/Game/System/GameSystemSceneController.cpp:81` |
| Execute entries | 4,096 | `src/Game/Scene/SceneObjHolder.cpp:322` |
| Live actors | 2,560 | `src/Game/LiveActor/AllLiveActorGroup.cpp:5` |
| Clipping lists | 4 x 2,560; 64 clipping groups | `src/Game/LiveActor/ClippingActorHolder.cpp:10-17`, `src/Game/LiveActor/ClippingGroupHolder.cpp:125` |
| Gravity fields | **128, no unregister** | `include/Game/Gravity/PlanetGravityManager.hpp:52` |
| Collision parts | 512 per zone per category; 32 zones | `include/Game/Map/CollisionCategorizedKeeper.hpp:39,56` |
| Sensors | MapObj 1,024, Character 1,024, Simple 2,048, Eye 512, Ride 128 | `src/Game/LiveActor/SensorHitChecker.cpp:169-174` |
| Shadows | 1,280 controllers (add-only) | `src/Game/LiveActor/ShadowController.cpp:27-28` |
| Zone switches | 128 per zone, 20 zones; global 128 (ids 1000-1127) | `include/Game/Map/StageSwitch.hpp:23,57` |
| Sleep controllers / switch watchers | 1,024 / 256 | |
| Camera chunks | 1,024 | `src/Game/Camera/CameraParamChunkHolder.cpp:12` |
| Area managers | 80 types; per type 16-160 | `include/Game/AreaObj/AreaObjContainer.hpp:31` |
| Demo casts | 512 actors / 64 layouts / 128 NameObjs; 32 groups x 192 | `src/Game/Demo/DemoDirector.cpp:22` |
| Resource holders / archives | 512 / 384 | `ResourceHolderManager` |
| Draw-buffer models per group | 1-256 (MapObj 256, NPC 80, Planet 64) | `SceneNameObjListExecutor.cpp:289+` |
| Child zones | 24 | `include/Game/Scene/StageDataHolder.hpp` |

### 2.5 Resource loading at runtime

- **Mounting is synchronous.** `ResourceHolderManager::createAndAddInner` mounts a missing archive synchronously (`MR::mountArchive`, `src/Game/System/ResourceHolderManager.cpp:98-119`). It then builds the holder via `startFunctionAsyncExecuteOnMainThread` + `waitForEndFunctionAsyncExecute`.
- **The game thread *is* the main executor thread during play.** So the function runs inline and **no task is queued**, after which `waitForEnd` reads past the end of its list (`src/Game/System/FunctionAsyncExecutor.cpp:117-129`). The spike adds a three-line guard (return if no task was found), after which Boo and Dry Bones archives loaded and spawned in 1.3-1.8 ms each.
- **Planet archives** (several MB with their LOD variants) should be preloaded on a worker thread while the user browses: `mountAsyncArchive` plus a per-frame `isLoadedObjectOrLayoutArchive` poll. Only then is the actor created.
- The 32-archives-per-object and 512-holder limits apply.

### 2.6 Reload from the model

**Mechanism.**
- `MR::requestChangeStageAfterMiss` performs a type-5 `requestGalaxyMove` to the same stage and scenario (`src/Game/Util/SequenceUtil.cpp:31-45`): a full scene destroy and re-init.
- **The file cache survives a same-stage reload** (`src/Game/System/GameSystemSceneController.cpp:122-124, 473-477, 523-531`), and stage archives are mounted from it (`src/Game/Scene/StageFileLoader.cpp:14-17`).
- The spike edited a placement row inside the loaded archive's memory, reloaded, and the trace showed the moved Octoomba (`l_id 66`, y −5788.1 → −4788.1). The reload reads what is in memory.

**Production path.** The EditModel serialises changed zone archives (5.2) into
editor-owned buffers. A native **archive override** then serves them in place of
the file cache entry, through the same DVD layer the mod overlay uses
(`native/platform/dvd/dvd_host.cpp`). This covers row-count, table and file
changes, which in-place byte edits cannot.

**Cost.** Measured 3,871 ms from request to normal play on Good Egg mission 1:

| Part | Time | Plan |
| --- | --- | --- |
| Wipe out and in | About 1 s | Replace with a short fade for editor reloads |
| Scene init | One 1.5 s frame | Placement plus resource holders. Partly irreducible |
| Shader pipeline gate | 1.5 s | The native gate waits for pipelines and timed out (`[gx stage ready] ... residual_wait_ms=1500 ... timeout-proceed`). Skip it on a same-stage reload: the pipelines exist |

Target: **about 2 s**. A profile of the 1.5 s init is spike S3 (section 7).

**Editor state across reload.**
- The EditModel, the camera, selection, undo stack and play marker all live in native editor state, **outside the scene heap**, so they survive.
- After reload, SceneSync re-binds editor ids to the new actors. The placement code can record `(row address → actor)` as it creates them (the `[placement]` trace point is the hook, `src/Game/Scene/PlacementInfoOrdered.cpp:139-148`).

### 2.7 Simulation: Edit, Play, play-from-here

These follow UX section 3, agreed between UX and tech.

- **Edit** is a native GameScene nerve, like `GameScenePhotoMode` (`src/Game/Scene/GameScene.cpp:182-187, 633-641`):
  - Entered only where the pause menu could open (`isPermitToPauseMenu`).
  - Movement stops; `calcAnim` and draw keep running every frame (`src/Game/Scene/GameScene.cpp:226-238`), so the frame stays and edits re-render.
  - Audio and rumble pause as in photo mode.
  - The editor camera reuses photo mode's view override (`src/Game/Camera/PhotoCamera.cpp:128-176`).
  - Clipping is suspended for actors near the selection, so culled actors don't stay hidden (a known photo-mode limit).
  - Planets, map parts and collision objects compute their matrix inside movement (`src/Game/Scene/SceneExecutor.cpp:48-51`). Moving one while frozen calls `MR::calcAnimDirect` (`src/Game/Util/LiveActorUtil.cpp:619-628`) and resets its collision matrix.
- **Play.**
  - If anything is pending or dirty, reload from the model (2.6) and then teleport Mario.
  - If not, unfreeze and teleport.
  - Teleport: the play marker dropped along gravity; the smoke's `PETARI_STAGE_WARP` shows the warp path.
- **Return to Edit** freezes only; the played state stays visible. Edits made there still go to rows and re-create their actors; the spike re-created actors in a scene that had been playing for 1,000 frames. Played-state caveats are shown, not prevented: switches on, collected items dead, Mario's state.
- **Trail.** A ring buffer of Mario's position per frame (60 s x 60 frames), written at the seam the way route recording does, and drawn as a line strip.
- **Keep.** Copies a live actor's transform into its row, for classes the catalog marks as placement-owned (not rail riders, children, or actors that re-derive their position; the spike saw one snap back).
- **Save isolation.** During an edit session NAND writes go to `<user>/editor/scratch-NAND` (the native NAND is a host folder). On Power Star get, a hook at the end of `GameScenePowerStarGet` returns to Edit instead of `requestChangeStageAfterStageClear`.
- **Mission switch.** `requestGalaxyMove` to the same stage and another scenario (Level Select's path, `src/Game/Scene/GameScene.cpp:191-202`), skipping the intro. 2-4 s.

### 2.8 Undo

- **Operations.** Undo is an operation log on the EditModel, not on the scene. Each `Op` (set cell, add row, delete row, add column, move rail point, add zone, ...) stores the before and after values keyed by editor id.
- **Undo.** Applying an op's inverse is a normal edit: SceneSync re-derives the affected actors, re-creating them or marking them pending.
- **Drag coalescing.** A drag streams preview pokes and records **one** op on release.
- **Multi-object ops.** Duplicate, array and paste are one compound op.
- **Journal.** The op log is written to the project journal as it happens (UX "Autosave and the journal"), so a crash loses nothing. Replaying the journal onto the original archives rebuilds the project.
- **Undo across a reload** works because ops live in native editor state.
- **Undo across play.** The undo stack never includes play-test results; Keep is an explicit op.

### 2.9 Per-system notes

- **Gravity.**
  - `PlanetGravityManager::registerGravity` appends to `mGravities[128]` with no bounds check and insertion-sorts by priority (`src/Game/Gravity/PlanetGravityManager.cpp:90-112`). There is no unregister, and changing a priority later does not re-sort.
  - Shape and parameter edits: mutate the existing gravity and call `updateIdentityMtx`.
  - Delete: `mAppeared = false`.
  - Priority edits are pending until reload, and deleted fields still count against 128 until then.
  - A gravity is active when `mActivated && mValidFollower && mAppeared`; `mActivated` = SW_A on and SW_B off (`src/Game/Gravity/GlobalGravityObj.cpp:40-58`).
  - Visualisation reads the same objects (3.3).
- **Collision.**
  - Each actor loads `<name>.kcl` and `.pa` from its own archive (`src/Game/Util/LiveActorUtil.cpp:48-76`).
  - Parts are added to and removed from their zone with swap-remove (`validate/invalidateCollisionParts`, `src/Game/Util/LiveActorUtil.cpp:2359-2390`), so runtime add and remove are supported.
  - The zone comes from the current placement zone id, which must be set around `init` (2.2).
  - The collision matrix is updated in calcAnim and the CollisionDirector movement step. Frozen moves call `calcAnimDirect` plus a matrix reset.
- **Switches.**
  - `SW_*` values below 1000 are zone-local (a 128-bit set per zone, 20 zones); 1000-1127 are global (`src/Game/Map/StageSwitch.cpp:31-37`).
  - The zone of a switch is the row's placed zone, so an object moved to another zone keeps its number but changes its signal. The editor re-allocates switch numbers on such moves.
  - Wires in the UI are drawn from the EditModel, not from runtime listeners.
- **Rails.** Copied into the actor at construction (1.5), so a rail edit re-creates its riders. `RailRider` crashes (`BezierRail::BezierRail`) when a class that needs a rail gets `CommonPath_ID = -1`; the spike hit this with a Launch Star. The catalog records required links (2.11).
- **Cameras.**
  - Camera-area edits change the `.bcam` chunk and the CubeCamera area.
  - Chunks are created at placement and sorted once, so camera edits are pending until reload.
  - The exception is the *preview*: the editor applies a chunk's parameters to a temporary camera directly, for the UX camera preview.
  - The cap is 1,024 chunks.
- **Areas.** `AreaObj::init` registers with its manager and has no remove (`src/Game/AreaObj/AreaObj.cpp:38-66`). Delete = `invalidate()`; edit = invalidate plus re-create.
- **Demos** are pending until reload. A demo's cast is gathered at init (`src/Game/Demo/DemoDirector.cpp`, `DemoCastGroup`).
- **Zones.** No per-zone unload exists, so zone edits are pending until reload.
- **Children** (`ChildObjInfo`) are created by their parent's `init` from rows matched by `ParentID`. A synthesized parent row must carry its children in the same synthetic zone data, so `getChildObjNum` (`src/Game/Util/SceneUtil.cpp:230-239`) finds them. A synthetic "zone page" (below) solves this, rails and groups at once.
- **Synthetic zone page.** In production, rows created live go into one **editor page per zone**: a set of synthetic tables (ObjInfo, ChildObjInfo, Path...) registered with the zone. `getChildObjNum`, the rail lookups and `JMapIdInfo` then resolve through the same holder. It is a `StageDataHolder`-shaped object that answers for its own address range.

### 2.10 Threads and input

- **Threads.**
  - The game runs on the macOS main thread under an emulated OS with a CPU baton (`native/platform/os/os_thread.cpp:1-25`).
  - An ImGui frame is open for the whole game frame (`native/app/frame_seam.cpp:649-653`), but the overlay is drawn **after the baton is released** (`native/app/frame_seam.cpp:636-647`), while audio and loader threads may run game code.
  - Therefore: UI code never touches actors. It enqueues `Command`s; the seam, still holding the baton (`native/app/frame_seam.cpp:612-635`), drains the queue into the EditModel and SceneSync, then publishes a **snapshot** for the panels (selection, visible actors with bounds and names, capacities, heap).
- **Input.**
  - Today every SDL event goes to ImGui *and* the game (`native/app/frame_seam.cpp:449-466`). The Home menu doesn't use ImGui input at all.
  - Edit mode gates game input on `ImGui::GetIO().WantCapture*` and diverts camera input the way `photoInput()` does (`native/input/wpad_host.cpp:624-702`). It also releases held game inputs on entry.
  - Keyboard modifiers are ignored by the game's key path (`input_sdl3.hpp:56-59`), so Cmd shortcuts must be consumed before it.

### 2.11 The object catalog

One generated table (`native/editor/catalog/*.json`, generated, plus a hand-curated overrides file) drives the Browser, the Inspector, Checks and SceneSync. One row per class:

| Column | Source |
| --- | --- |
| Class (`en_name`), Japanese name | `cCreateTable`, ObjNameTable |
| **Display name, one-line description** | Curated (the top ~150 first, then the rest) |
| Category (enemy, item, platform, gravity, area...) | Creator source file directory + curated |
| Archives | `getMountObjectArchiveList` evaluated per class |
| Args: index, name, type, default, enum values, unit | Static scan of `getJMapInfoArgN*` call sites (about 614 in 240 files), names curated |
| Switches: role (`SW_A`, `SW_APPEAR`...), direction (read, write) | Scan of `useStageSwitchRead*/Write*`, `needStageSwitch*`, `listenStageSwitch*` |
| Links: rail, child, group, camera, demo, message; required? | Scan: unconditional `initRailRider` = required, guarded by `isConnectedWithRail` = optional. **Proven by the class sweep** |
| Flags: placement-owned transform (Keep), gravity-aware, has collision, re-derives position | Scan + sweep |
| Appear SE, thumbnail model | Curated / first model in the archive list |

**Class sweep.** A harness using the spike mechanism spawns every creator with
empty links in a test stage and records which crash, assert or stay invisible.
That empirically proves the required links, which Whitehole users discover by
crashing. The sweep is cheap: no new mechanism, one background smoke per batch.

The community galaxydatabase has no licence. It is an optional,
user-triggered import into the user folder only (UX Q4).

---

## 3. Rendering for the editor

### 3.1 Where editor drawing plugs in

| Layer | Path | Used for |
| --- | --- | --- |
| **World overlays** (depth-tested) | GX immediate mode in a pass at the end of `GameScene::draw3D`, after the last 3D categories and **before** `MR::clearZBuffer()` (`src/Game/Scene/GameScene.cpp:461-463`) | Gravity shapes, areas, rails, wires, gizmo handles, trail, ghosts' boxes, brush wireframes |
| **X-ray** (no depth test, faint) | The same pass again with `GX_ALWAYS` at low alpha | Occluded parts of the selection and wires ("see through planets") |
| **Selection / hover** | Extra draws of the actor's own J3D model (3.4) | Outline and tint |
| **2D annotations** | ImGui draw lists, projected with `MR::calcScreenPosition` (`src/Game/Util/CameraUtil.cpp:65-133`) | Labels, switch numbers, handle hit areas, the gizmo's screen-space parts |
| **Panels** | ImGui docking (section 4) | Everything else |

- **GX for world overlays.** The game's own `TDDraw` (`src/Game/Util/DirectDraw.cpp`, API in `include/Game/Util/DirectDraw.hpp:9-44`) already runs on Aurora: the shadow code uses it. It provides lines, circles, cylinders, sphere parts, filled boxes and 3D textures. Aurora expands `GX_LINES` / `GX_LINESTRIP` / `GX_POINTS` to quads and honours line width (`aurora-reference/lib/gx/shader.cpp:965-1050`).
- **Depth.** `TDDraw::setup` turns depth testing off (`src/Game/Util/DirectDraw.cpp:59-65`). The editor pass calls it, then sets `GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE)`: test, no write.
- **Registering the pass.** It is registered as a NameObj on its own `DrawType` through `CategoryList`, which is how the game adds draw passes. No change to `draw3D` beyond that registration.
- **Performance.** Everything is batched per frame from the snapshot. Gravity shapes are drawn as line meshes (32-segment circles), so even a busy galaxy is a few thousand vertices.

### 3.2 What each thing looks like (implementation notes for UX section 9-11)

- **Gravity.** One routine per gravity class, reading the live `PlanetGravity` object:
  - Point: sphere plus range shell
  - Parallel: box or cylinder with arrows
  - Cube: box plus face arrows
  - Disk and disk-torus: disk, torus
  - Cone, segment: their shapes
  - Range and priority colouring comes from the object.

  Field arrows are sampled with the game's own gravity function (`MR::calcGravityVector`) on a grid around the cursor, so the view shows exactly what Mario feels.
- **Areas.** The area's shape (`AreaObj` form: cube, sphere, cylinder, bowl) as a translucent volume plus edges.
- **Rails.** The Bézier evaluated with the game's `BezierRail` code, so the drawn curve matches the ride. Handles are points and tangents.
- **Cameras.** A frustum from the chunk's parameters at the area's centre; preview uses the real camera (2.9).
- **Wires.** A curved line between the two actors' anchor points, colour-coded by link type and directed (UX section 8).

### 3.3 Picking

No ID buffer is needed:

1. **Ray.** From the cursor: `MR::calcWorldRayDirectionFromScreen` (`include/Game/Util/CameraUtil.hpp:12-18`) using the editor camera.
2. **Candidates.** Ray against every live actor's clipping sphere (`ClippingActorInfo` centre and radius, `include/Game/LiveActor/ClippingActorInfo.hpp:11-31`), plus the EditModel's ghost and editor-only items.
3. **Refine.**
   - Each candidate's model shapes have bounding boxes (`J3DShape::mMin` / `mMax` / `mRadius`, `libs/JSystem/include/JSystem/J3DGraphBase/J3DShape.hpp:246-248`). Transform them by the joint matrix and test.
   - For the front few, an optional exact ray-triangle test against the shape's vertex positions (static models; skinned models use the box).
4. **Occlusion and ground.** `MR::getFirstPolyOnLineToMap` (`src/Game/Util/MapUtil.cpp:186-245`) gives the nearest map hit. Anything behind it is skipped unless x-ray is on, and the hit point and normal are "the ground" for drop and place.
5. **Planets.** A planet is picked through its collision (the map ray reports the parts), not its model, which is far more precise.

This runs at the seam on the game thread (the pattern `native/app/collision_probe.cpp` uses), on hover only when the cursor moves.

### 3.4 Selection outline and hover

- **Outline.**
  - Draw the selected actor's model a second time with front faces culled, the model matrix scaled about its centre by about 2-3% (screen-space constant), a flat colour, and depth test on: the classic hull outline.
  - The model is re-submitted through `J3DModel::entry`-style direct draw with a material override (TEV stage forced to a konst colour).
  - Draw the real model once more on top with depth `LEQUAL` so the hull shows only at the silhouette.
- **Hover** uses a lighter tint via the same override without the hull.
- **Fallback:** for skinned or very concave models where the hull looks wrong, draw the shape bounding boxes' edges instead.
- **Cost:** one extra draw of one or a few models.
- **Later alternative:** a screen-space edge filter. Draw selected models' ids into destination alpha (`GXSetDstAlpha`), then `GXCopyTex` and an edge-detect quad. Better quality, but more Aurora-side work. Not needed for M1.

### 3.5 Model thumbnails

- **Standalone J3D drawing already exists.** `native/gx/model_scene.cpp` loads a `.bdl`/`.bmd` from a RARC (`petari_probe_load_model`) and draws it with its own viewport, projection and look-at, with no scene (`:73-101`). It is used by `petari_gx_probe`.
- **Thumbnail pass.** At an editor frame boundary, before the scene's draw:
  - Draw the model into a corner of the EFB.
  - `GXCopyTex` the region (with clear) to a texture.
  - Read it back (the readback path in `native/gx/patch_aurora_readback.py`).
  - Write a 128 px PNG to `<user>/editor/thumbs/<class>.png`.
- **Cost:** one thumbnail per frame while the Browser is open, plus a "build all" pass (about 1,200 classes, about 20 s at 60 per second) on first use.
- **Into ImGui:** `aurora_imgui_add_texture` takes CPU RGBA (`aurora-reference/lib/imgui.cpp:245-298`), fine at 128 px. Animated or multi-part classes use the catalog's chosen model, or an icon.
- **Nothing is shipped.** Thumbnails are derived from the user's disc into their user folder.

### 3.6 The game in a sub-rectangle

- **Two patched functions decide where the game image lands:**
  - the present viewport (`calculate_present_viewport`, `native/gx/patch_aurora_present.py:248-262`)
  - the EFB sizing in `get_window_size` (`:345-370`)

  `petari_present_image_rect` reports the result to input (`native/gx/present/present_aurora.cpp:534-553`, `native/app/frame_seam.cpp:484-510`).
- **Docked mode.** The editor supplies the central dock node's rect. The present viewport uses it, the EFB is sized to its aspect, and `image_rect` returns it, so pointer mapping just works.
- **ImGui renders in its own full-surface pass** (`aurora-reference/lib/aurora.cpp:264-370`), so panels paint around the image.
- **Resizing** a panel resizes the EFB (`setContentAspect`); copy textures drop for one frame. Resizes are debounced while dragging a splitter.

---

## 4. UI technology

### 4.1 Options

| | **Dear ImGui (docking)** | **AppKit / SwiftUI panels** beside or over the Metal view |
| --- | --- | --- |
| Already there | Yes: 1.91.9b **docking branch** is what Aurora fetches (`aurora-reference/extern/CMakeLists.txt:224-249`); the Home menu draws with it and the game's fonts are in its atlas | No AppKit UI exists; the window is an SDL3 window created by Aurora |
| Docking, tabs, splitters | Built in (`DockSpaceOverViewport`) | `NSSplitView` / SwiftUI `HSplitView`; tear-off panels mean extra `NSWindow`s and window management |
| Viewport integration | Same surface, same frame; the game image is a dock node (3.6) | Must carve the SDL Metal view out of an AppKit layout, or overlay transparent windows. Resizes and focus juggle two toolkits |
| Data access | Immediate mode reads the snapshot each frame; no bindings | A model layer, Swift-C++ interop, and a thread hop to the game thread for every read and write |
| Text input | Fine for names and numbers; SDL3 delivers text and IME events, which the ImGui SDL3 backend handles | Native: best (spell check, IME, services) |
| Trackpad and scroll | SDL3 gives precise scroll and **pinch** (`SDL_EVENT_PINCH_UPDATE`, already used by photo mode); no rotate gesture in SDL3 (add an `NSEvent` monitor for magnify and rotate) | Native gestures everywhere |
| Accessibility | **Weak**: no VoiceOver tree | Full VoiceOver |
| Look | Fully custom: the Galaxy pause-screen style the Home menu already uses (`native/HOME_MENU.md`). Crisp text needs a second UI font at small sizes (UX Q2) | Mac-native look, which clashes with the game style the user asked for; restyling AppKit to Galaxy's look is a lot of work |
| Effort to a pro editor | Low to medium: panels, a property grid, tree views, tables and drag-drop are all stock | High: a second UI stack, a C++/Swift bridge, threading, two input routings, two theme systems |
| Testability | The existing software rasteriser renders ImGui draw data to PNG offline (`petari_home_menu_render`), so panels can be golden-image tested in CI | XCUITest; harder in this repo's CI |

### 4.2 Recommendation: Dear ImGui, docking, with targeted native bridges

ImGui wins on everything that matters here: integration with the live viewport,
speed of building, the Galaxy look, and testability. Its real weaknesses are
accessibility and native text niceties. Accessibility is acknowledged as a gap;
the editor is a creative tool for a sighted pointer user, as in every game
editor in this space. The native feel the user asked for (trackpad gestures,
Cmd shortcuts, undo/redo, file dialogs) is handled by small bridges instead:

- **File dialogs:** `SDL_ShowOpenFileDialog` / `SDL_ShowSaveFileDialog` (SDL 3.4, available; nothing in native/ uses them yet). These are native `NSOpenPanel` / `NSSavePanel` sheets.
- **Menu bar:** a real `NSMenu` (File, Edit with Undo and Redo, View, Object, Window) built in a small Objective-C++ file, with actions that enqueue editor Commands. This gives Cmd+Z / Cmd+Shift+Z, Cmd+S and Cmd+C/V/D the Mac way, plus the Help search.
- **Gestures:** an `NSEvent` local monitor for magnify, rotate and smart-zoom, feeding the editor camera. SDL already gives precise scroll and pinch.
- **Keyboard:** the editor consumes Cmd-modified keys before the game's key path, which ignores modifiers (`native/input/include/petari/input_sdl3.hpp:56-59`).
- **Text:** `SDL_StartTextInput` while an ImGui text field is active, so IME and dead keys work.
- **Fonts:** Galaxy fonts from the disc for titles and buttons (as the Home menu does). The system UI font for dense fields, loaded at runtime from the system (not shipped). See UX Q2.

**Enabling docking.**
- Set `ImGuiConfigFlags_DockingEnable` in the editor only.
- Call `DockSpaceOverViewport` with a pass-through central node at the overlay point (`native/app/frame_seam.cpp:647`).
- Persist the layout in `<user>/imgui.ini`, which Aurora already sets (`aurora-reference/lib/imgui.cpp:87-90`).
- Multi-viewport (panels as separate OS windows) is **not** available with the WGPU backend; everything stays in the one window, which suits a Mac fullscreen workflow anyway.

---

## 5. Saving and sharing

### 5.1 Project, mod, and the two output forms

These follow UX section 17.

**The project** (`<user>/editor/projects/<Name>/`) holds:

| Item | What it is |
| --- | --- |
| `project.json` | Galaxies touched, settings, editor-only data |
| `journal.oplog` | The undo operation log, appended live |
| `assets/` | User-made content: brushes, imported meshes and textures, catalog overrides |
| `cache/` | Generated archives, thumbnails; deletable |

**Cmd+S writes the playable mod** to `<user>/mods/<Name>/`, in one of two forms:

| Form | Contents | For |
| --- | --- | --- |
| **Patch** (default) | `mod.txt`, `preview.png`, `edits/<Galaxy>.json` (row operations keyed by original identity, 5.3), `assets/` (user-made only), and the project for remixing | Sharing between Petari users. **Contains no Nintendo data** |
| **Full archives** | `files/StageData/...arc` etc., written by our writers (5.2). Optionally the patch alongside | Whitehole round trips; Dolphin/Riivolution; other tools |

**Turning a patch into archives.** The mod loader (`native/platform/dvd/mod_folder.cpp`) gains one step at start-up:
- For each enabled patch mod, read the user's own disc archives, apply `edits/*.json`, build generated assets (terrain, 6), and write the results to `<user>/mods-cache/<Mod>/<hash>/files/...`.
- The existing overlay then serves them exactly like a full-archive mod. The cache key is a hash of the patch, assets and disc file, so this runs once.
- Conflicts and precedence keep the current rules. Two patch mods that touch the same archive *merge* when their operations don't touch the same rows (an improvement on archive-level "first wins"). Otherwise the higher-priority mod wins, with a log line.

### 5.2 Writers (native C++, shared by the editor and the mod loader)

| Writer | Scope | Notes |
| --- | --- | --- |
| **BCSV** | Read/write all types; add or remove rows and columns; create tables | Column layout: keep the original field order and offsets when columns are unchanged (so unedited tables rebuild byte for byte); new columns are appended with natural alignment. Strings: Shift-JIS pool (macOS `iconv` with CP932; the Python tool uses `shift_jis`), de-duplicated, padded with `@` to 32 bytes as the disc is. A field-name dictionary (from the decomp's string literals plus known table schemas) maps hashes back to names |
| **RARC** | Full rebuild: folders, files, the names table, node and directory ids, file flags (MRAM/ARAM/compressed), 32-byte alignment | The Python tool's byte-for-byte rebuild is the oracle: `native_stage_edit` gains a C++ twin test over every disc archive |
| **Yaz0** | Encode and decode | The game and mod loader accept plain RARC, so the editor writes **uncompressed** for its own cache and reloads (fast). It compresses only for Full-archive export, with a proper lazy-matching encoder (the Python greedy encoder is the fallback) |
| **Rails** | Regenerate `CommonPathInfo` and the numbered `CommonPathPointInfo.<n>` files from the rail objects, keeping original order for untouched rails | Because the point table is chosen by row index (1.5) |
| **Camera** | `.bcam` as BCSV with the chunk schema | Rows keyed by chunk id string |

### 5.3 The patch format

`edits/<Galaxy>.json` is a list of operations against **original identities**:
`(archive path, table path, original row index, original row fingerprint)`. The
fingerprint is a hash of the original row's cells; it detects a disc that
differs (another region or a modded base).

```json
{ "format": 1, "game": "RMGE01",
  "archives": { "StageData/SoramameZone.arc": [
    { "op": "set",    "table": "jmp/placement/layera/objinfo", "row": {"index": 12, "fp": "9c1e..."},
      "cells": { "pos_y": -12427.7, "Obj_arg0": 3 } },
    { "op": "add",    "table": "jmp/placement/layera/objinfo", "id": "u:4f1c...",
      "cells": { "name": "Kuribo", "l_id": 9001, "pos_x": 0, "pos_y": 0, "pos_z": 0 } },
    { "op": "delete", "table": "jmp/placement/layera/objinfo", "row": {"index": 3, "fp": "51ab..."} },
    { "op": "addColumn", "table": "...", "name": "CommonPath_ID", "type": "short", "default": -1 },
    { "op": "addFile", "path": "jmp/placement/layerb/objinfo", "from": "schemaOf:jmp/placement/layera/objinfo" }
  ] },
  "newArchives": { "StageData/MyGalaxy.arc": { "template": "empty-galaxy", "ops": [ ... ] } }
}
```

- **Applying is deterministic:** `set` ops first, then `delete`, then `add` in order. Deleting by fingerprint is idempotent.
- **A mismatched fingerprint** (a different disc) refuses that archive with a clear log line, and the editor offers to re-base the project.
- **New galaxies** are built from **templates**: tiny skeleton archives described in JSON (empty tables with the disc's schemas), so they need no disc data either.

### 5.4 Diff against the original

- **The Changes panel** (UX section 16) is the same computation as the patch: the EditModel versus the parsed original, row by row by identity, cell by cell.
- **For a Full-archive mod from another tool**, "Open in editor" parses the mod's archive and the disc's, matches rows by `(table, l_id, name)` then by nearest position, and turns the differences into ops. This is how Whitehole mods become editable projects (5.6).

### 5.5 Validation before saving

The Checks run on the EditModel at every op (cheap, incremental) and block saving only for crash-class errors:
- missing required links (catalog, 2.11)
- capacity overflows (2.4)
- switch ids out of range
- rails with fewer than 2 points
- objects outside every zone's bounds
- unknown classes
- missing archives
- duplicate `l_id` in a table
- camera areas without a `.bcam` chunk, and the reverse
- start points without a camera

### 5.6 Whitehole compatibility

- **Reading Whitehole mods.** They are Full-archive mods (Yaz0 RARC); the overlay already loads them (native/MODS.md). The editor opens them through 5.4.
  - Whitehole may add columns or write `type` instead of `name`. Both are understood, because columns are optional and kept.
  - Whitehole's object database names are only shown if the user imported it.
- **Writing for Whitehole.** Full-archive export writes standard Yaz0 RARCs with the disc's table layout, so Whitehole, Dolphin and Riivolution open them.
  - Editor-only data never goes into the archives.
  - New galaxies also need the out-of-archive entries (5.7), which Whitehole users add by hand today; the export writes them as files with a README.
- **Terrain** made by the editor is native-format KCL/BDL (section 6), so it stays portable.

### 5.7 New galaxies

**What already works.**
- The overlay can add new directories, and `ScenarioDataParser` finds any `StageData/<G>/<G>Scenario.arc` (`src/Game/System/ScenarioDataParser.cpp:159-189`, `native/platform/dvd/dvd_fst.cpp:357-366`).
- `petariDirectStageMove` enters any parsed galaxy (`src/Game/System/StorySequenceExecutor.cpp:1048-1067`).
- The stage itself needs only its own files:
  - `<G>.arc` and its zone archives (tables, `StartInfo`, `CameraParam.bcam`).
  - The Scenario archive (`ScenarioData` and `ZoneList`).
- Lighting falls back to the default area light, and the preview texture is guarded.

**What breaks** (all compiled into the executable or missing guards):

| Problem | Effect today | Fix (native, `PETARI_NATIVE`) |
| --- | --- | --- |
| `GalaxyID.bcsv` (embedded; 55 rows) has no row | Lookups read **past the table**: PowerStarNum 65, GrandGalaxyNo 115, wild `OpenCondition` pointers (`include/Game/Util/JMapInfo.hpp:186-217`) | A **mod galaxy registry**: rows from each mod's `galaxy.json`, consulted before the embedded table |
| `GameEventFlagTable` (compiled in) has no flags for it | **Crash** for any galaxy with a Power Star: `GameEventFlagChecker::canOn/isOn` use a null `findFlag` result (`src/Game/System/GameEventFlagChecker.cpp:14-16,67-69`), reached from `isOpened`, `getGalaxyNumCanOpen` and the pause star list | Registry-supplied `Galaxy`/`Appear`/`StarPiece`/comet flags, plus null guards |
| No `GalaxyName_<G>` / `ScenarioName_<G><N>` messages | **Crash** on mission select, the stage result and the name plate: a null message reaches `ReplaceTagProcessor` (`src/Game/Screen/LayoutCoreUtil.cpp:19-23`) | A **message overlay**: the lookup consults the registry's names (all three disc languages, English fallback) before `Message.bmg`. Mods never replace `Message.arc`, so they don't conflict |
| BGM and sound banks (`AudStageBgmWrap`, `AudSceneMgr`, compiled in) | Silence | `galaxy.json` names an existing galaxy to borrow music and banks from ("music like Gusty Garden") |
| `SpinDriverPathStorage` allocates 50 and appends unchecked | **Heap overflow after 2 new galaxies** | Size from the parsed galaxy count |
| `ScenarioDataParser` fixed 64 (48 used) and the save's 64 galaxy slots | Unchecked beyond 64 | Raise or check; the editor caps total galaxies with a Check |
| Level Select and `--stage` use the hard-coded `kGalaxies` (`native/home_menu/launch_stage.cpp:12-55`) | Not listed | Build the list from the parser, with a **Mod galaxies** section |
| Save: `GALA` records keyed by a 16-bit name hash; unknown records are dropped at the next save | Progress lost when the mod is off; possible hash collision with a real galaxy | The registry rejects colliding names. Mod galaxies' records go to a **sidecar** `<user>/mods-saves/<Mod>.bin`, and `GameData.bin` stays vanilla-shaped |
| Observatory: domes, map panes, `Mini<G>` miniatures (names hard-coded; a dome holds at most 5) | Not shown | Not in v1: mod galaxies are reached from Level Select (UX agreed). Domes later |

**`galaxy.json`** (in the mod, user-made, so no Nintendo data):
- name and short name per language
- mission names
- stars per mission, comet flags
- star colour
- music and banks "borrowed from"
- an optional preview image

The editor's new-galaxy flow (UX section 14) writes it with the Scenario
archive and the skeleton stage archives from templates (5.3).

**Portability.** A new galaxy needs Petari's registry, because the real game has
no data-driven way to add one. On a Wii or in Dolphin, the equivalent is an
executable patch (the community uses code patches for this). Full-archive export
of a new galaxy therefore warns that it needs such a patch elsewhere. *Edits* to
existing galaxies stay fully portable.

---

## 6. Terrain (later milestone, designed now)

### 6.1 What the game already gives us

- **A planet is just an archive.** `ObjectData/<Name>.arc` holds `<Name>.bdl` (model), `<Name>.kcl` + `<Name>.pa` (collision and its attributes), optional animations, and sibling archives `<Name>Low`/`Middle`/`Bloom`/`Water`/`Indirect`.
- **New planet classes need no code.** Any name in `PlanetMapDataTable` becomes a `PlanetMap` class (`src/Game/NameObj/NameObjFactory.cpp:8104`, `src/Game/Map/PlanetMapCreator.cpp:282-338`). `PlanetMap::init` loads the model, LOD, bloom and water sub-models, collision, `MoveLimit.kcl` and `WaterSurface.kcl`, and starts animations (`src/Game/Map/PlanetMap.cpp:37-157`).
- **Collision from memory already exists natively.**
  - `CollisionParts::init(..., KCLFile*, ...)` takes a host-side descriptor and **uses it in place** (`include/Game/Map/CollisionParts.hpp:22-25`, `src/Game/Map/KCollision.cpp:80-86`), so geometry can change live.
  - `DynamicCollisionObj::createCollision` builds one at runtime (`src/Game/MapObj/DynamicCollisionObj.cpp:157-198`), tested in `native/tests/kcl_server_tests.cpp`.
  - Big-endian `.kcl` buffers convert with `PetariNative::KCL::convertInPlace` (`native/resource/kcl_collision.cpp:103-194`).
- **Models from memory.** `J3DModelLoaderDataBase::load` / `loadBinaryDisplayList` accept any BMD/BDL buffer (`J3DModelLoader.hpp:294-298`). There is no non-J3D model path.
- **Writers to start from.** None exist in production, but the tests have exact templates:
  - `buildSyntheticKcl` (`native/tests/kcl_tests.cpp:82-143`), with octree branches and shared lists.
  - A minimal complete BDL builder: one joint, one material (MAT3 + MDL3), one shape, one texture (`native/tests/j3d_load_tests.cpp:118-123`; every block in `j3d_model_tests.cpp`).
  - A Python KCL reader (`native/tools/observatory_static_collision.py:121-132`).
- **KCL limits.** u16 indices (at most 65,535 triangles per file), 512 collision parts per zone, 32 hits per query. Surfaces come from the `.pa` attribute row per triangle: `Floor_code` (Normal, Death, Ice, Sand, Slip, Damage*...), `Wall_code`, `Sound_code`, `Camera_through` (`src/Game/Map/CollisionCode.cpp`).
- **Gravity is analytic only:**
  - Point, Cube, Disk, DiskTorus, Cone, Plane, PlaneInBox, PlaneInCylinder, Segment, Wire.
  - Shared fields: `Range`, `Priority`, `Power`, `Inverse`.
  - Types and parameters are in `src/Game/Util/GravityUtil.cpp` and `src/Game/Gravity/GravityCreator.cpp`.
  - SMG1 has **no mesh gravity**.
- **Rendering limits.**
  - Distinct planet models: 64 per draw-buffer group.
  - Materials are batched across instances, so fewer unique materials is cheaper.
  - Every new GX state combination compiles a pipeline, and draws still compiling are skipped after a 6 ms budget (`native/tools/PIPELINE_SEEDING.md`). Reusing the game's TEV setups avoids pop-in.
  - Aurora decodes every GX texture format the disc uses (CMPR, I4/I8, IA4/IA8, RGB565, RGB5A3, RGBA8, C4/C8 with IA8/RGB565/RGB5A3 palettes).

### 6.2 Options

| | **A. Generate native formats** (KCL + .pa + BDL, registered as a planet) | **B. Petari-native terrain** (our own mesh format; the port draws and collides it directly) |
| --- | --- | --- |
| Collision behaviour | The game's own KCL server, so every surface code, wall jump and camera-through works | Must still feed the KCL server (the only collision the game has), so B ends up building a `KCLFile` anyway |
| Rendering | A J3D model like every planet: lighting, fog, bloom, LOD, shadows, draw buffers | A custom draw actor emitting GX: lighting, shadows, bloom and fog must be re-implemented to match |
| Portability | Mods work in Dolphin and on a Wii and open in Whitehole | Petari-only |
| Effort | KCL writer (moderate, templates exist), BDL writer (the hard part: MAT3 materials) | Mesh format and custom draw (moderate), plus parity work that never ends |
| Live editing | Build KCL in memory (`KCLFile`) for collision; preview draw for looks; full BDL on Bake | Native |

### 6.3 Recommendation: A, with a live preview path

**One pipeline, two speeds:**

1. **Editing (live).**
   - Brushes and imported meshes live in the project as **our own mesh data** (`assets/terrain/*.json` plus binary buffers; user-made, so shareable).
   - On every edit, the editor triangulates the result (CSG for brushes) and builds a `KCLFile` **in memory**: positions, normals, prisms and an octree, which takes milliseconds for blockout-sized meshes. A `CollisionParts` of the right zone uses it in place, so Mario walks on a brush the moment you release it.
   - Looks during editing are an editor draw pass (3.1): flat-shaded triangles with a per-surface tint, or the chosen game material's base texture.
2. **Bake (build, on the user's machine).**
   - The patch builder (5.1) writes `ObjectData/<Part>.arc` containing a real BDL, `<Part>.kcl` and `<Part>.pa`.
   - It adds a `PlanetMapDataTable` row and an `ObjNameTable` row, so the part is placed like any planet.
   - Everything is native format, so the result is portable.

**Choosing A means "Petari-native" exists only as the editor's preview. No Petari-only format ships**, and nobody has to accept non-portable mods.

**Materials from the game, without copying them into the mod.**
- A brush face's "look" is a reference: `donor model + material index` (for example the grass of `HoneyBeePlanet`) plus our texture-lock UVs.
- At bake time on the player's machine, the BDL writer clones that material's MAT3 entry and its TEX1 textures from the player's own disc.
- The shared mod contains the reference, never the texture. The look is identical on every machine with the game.
- User-imported textures are the user's own: encoded to CMPR (opaque) or RGB5A3 (alpha) and shipped in the mod.

**The BDL writer subset.** Static meshes, one joint (or one per brush group), and materials either cloned (donor) or from three built-in templates (lit textured, lit vertex-colour, unlit). Tri-strips via a simple stripifier, position/normal/UV0 (+colour), and 16-bit indices. This is a small fraction of the J3D format. The test `BlockWriter` is the starting point, and the loader tests become its tests.

**KCL writer.**
- Triangles go to prisms. Normals are de-duplicated with a tolerance.
- The octree has a fixed top-level cell size (power of two around the mesh extent / 8), subdivided until a leaf holds 16 or fewer prisms or reaches a minimum size.
- Prism lists are shared by suffix, as the disc does.
- Meshes above 65,535 triangles are split into several parts by spatial cell, each its own archive, which also keeps octree queries fast.
- The `.pa` is a BCSV with the four code columns, one row per distinct surface.

**Gravity for custom planets.**
- Default: **fit analytic fields**.
  - A sphere-ish brush gets Point gravity.
  - A box gets Cube gravity with the right faces.
  - A cylinder gets Segment plus Disk caps.
  - A torus gets DiskTorus.
  - A flat platform gets PlaneInBox.
- Fields are kept in sync with the brush transform. These are normal `PlanetObjInfo` rows, so they stay portable.
- For an arbitrary imported mesh the editor proposes a fit (bounding sphere or box) and shows the field (3.2), so the user can see where it is wrong and adjust.
- A true **mesh gravity** (closest-point-on-surface, using the KCL octree) would be a new Petari-only class. It is *not* in v1. If added later, mods using it are labelled "Petari only" in Share and refuse to export as Full archives without a warning.

**Performance budget** (Checks warn above it):
- Up to about 20k triangles per part and 64k per zone.
- Up to 8 distinct materials per part (draw-buffer batching).
- Textures of 512 px or less.
- No more than 64 distinct new planet models per galaxy (the Planet draw-buffer capacity).
- New TEV setups are minimised by preferring cloned game materials, whose pipelines are already seeded.

### 6.4 Brushes and import (to detail in M5 and M6)

- **CSG.**
  - Convex brush primitives (box, wedge, cylinder, sphere, torus, stairs, arch) as plane sets.
  - Additive brushes are unioned; *Cut* brushes are subtracted.
  - Evaluated with a BSP or a robust mesh-boolean library (license check needed; e.g. Manifold, Apache-2.0) on a worker thread. Then the KCL update is swapped in on the game thread.
- **Texture lock** is world-aligned planar projection per face, with an optional curved projection on sphere and torus brushes.
- **Import.**
  - glTF 2.0 (cgltf, MIT) and OBJ (tinyobjloader, MIT).
  - Scale and up-axis normalisation.
  - Material-to-surface mapping by name.
  - Collision from the same mesh, a simplified mesh (meshoptimizer, MIT), or a separate file.
  - LOD via the planet `Low`/`Middle` archives, generated by simplification.

---

## 7. Milestones

Each milestone is a playable, shippable step. Sizes are in **worker-weeks** of
focused implementation and testing for one worker in this repo. The
integration overhead of the current flow (worktrees, the integrator, smokes) is
included. The UX order (UX section 20) maps onto these.

### M0 (inside M1): foundations

| Piece | Notes |
| --- | --- |
| `EditorRuntime` (native) | Row registry and synthetic zone pages, late connect with headroom reserved at load, clipping view groups, the `waitForEnd` guard, actor binding after reload. All are the spike's hooks, productised and unit-tested |
| EditModel and C++ readers/writers | BCSV and RARC, byte-for-byte rebuild over every disc archive (C++ twin of `native_stage_edit`) |
| Edit nerve and editor camera | From photo mode |
| Command queue and snapshot at the seam; input gating | |
| ImGui docking and the game in a dock node | 3.6 |

### Milestone table

| | Ships | Size | Main risks |
| --- | --- | --- | --- |
| **M1** Look and move | Edit/Play toggle (reload on Play), editor camera, picking and hover, selection outline, move/rotate/scale gizmo with drop-to-ground along gravity, raw-field Inspector, undo/redo, journal, Changes, save as a **Full-archive** mod (works with today's mod loader), play marker | **6-8** (M0 included) | Sub-rect EFB resizing; reload time; classes that misbehave when re-created (mitigated by the class sweep in M2; M1 re-creates only on commit); the outline on skinned models |
| **M2** Understand and create | Catalog (static scan plus **class sweep**, curated names for the top 150), Browser with thumbnails, add/delete/duplicate/array, named args with help, Checks, mission chips and ghosts, **patch mods** in the loader, Share zip and install | **6-8** | Catalog curation effort; Browser thumbnail capture in Aurora; patch-loader cache correctness |
| **M3** Systems | Gravity view and editing, rails (curves, handles, riders re-created), switches as wires, areas, cameras (preview live, edit pending until reload), trail, Keep, play-test save isolation, Power Star → back to Edit | **6-7** | Gravity cap and no unregister (Checks); camera chunk re-sort and reload; per-class gravity-shape coverage |
| **M4** New galaxies | Templates, zone placement, ScenarioData/ZoneList authoring, the **mod galaxy registry** (`galaxy.json`: GalaxyID rows, event flags, names via message overlay, borrowed music), null guards, Level Select from the parser, save sidecar | **5-7** | Galaxy data compiled into the executable (event flags, BGM, sound banks): every consumer must go through the registry, so S8 must find them all; the 16-bit save hash; the 64-galaxy caps |
| **M5** Terrain blockout | Brushes and CSG, surfaces, live in-memory KCL, preview draw, primitive gravity fit, **Bake** to KCL+PA+BDL with cloned game materials, registered as a planet | **7-9** | BDL writer (MAT3/SHP1/TEX1 correctness on Aurora), KCL octree build quality and speed, texture lock on curved brushes |
| **M6** Model import | glTF/OBJ, texture encoding (CMPR/RGB5A3/I8), material-to-surface mapping, collision simplification, scale and up-axis sheet | **4-5** | Large meshes (KCL size, draw cost), texture memory, non-manifold input |

Total: about 34-44 worker-weeks. M1 alone already beats Whitehole's loop:
live, undoable, no Dolphin round trip.

### Spikes to run first, in order

| # | Spike | Proves | Size |
| --- | --- | --- | --- |
| S1 | Live actor creation from synthesized rows | **Done** (section 8) | |
| S2 | **Class sweep** prototype: spawn every creator (about 1,180) with empty links in 3 test stages; record crash, assert or invisible | Which classes re-create safely; required links; the catalog's empirical half | 3-4 days |
| S3 | **Fast reload**: skip the pipeline gate and the wipe on same-stage reloads; serve zone archives from an editor buffer (archive override); profile scene init | ≤2 s reload; row-count changes via reload | 3 days |
| S4 | Docked viewport: game in an ImGui central node, input mapping, resize | 3.6 | 2 days |
| S5 | Selection hull outline through a J3D material override on Aurora | 3.4 | 2 days |
| S6 | **KCL round trip**: generate KCL for a box/sphere brush in memory, create a collision part from it live, walk on it | Terrain collision path (6) | 4 days |
| S7 | **Minimal BDL**: write a one-material, one-joint BDL from a mesh, with the material cloned from a disc model, and render it through `ModelObj` | Terrain rendering path (6) | 1 week |
| S8 | **New galaxy loads**: a skeleton galaxy with one Power Star, outside `GalaxyID.bcsv`, entered through a registry prototype; collect the star, save, reload. Find every crash site | 5.7 | 3-4 days |

S2 and S3 de-risk M1 and M2. S6 and S7 de-risk terrain and should run early,
though terrain ships last, because their answer could change the project
format (section 6).

---

## 8. The spike (run)

### 8.1 What ran

**Setup.**
- Branch `wt/editor-spike` in `petari-worktrees/editor-spike`. Throwaway; nothing committed to `main`.
- Background stage smoke on `EggStarGalaxy` mission 1, through `build/locked-app.sh`.
- A driver (`PETARI_EDITOR_SPIKE=1`) runs after the smoke's own checks, inside `GameScene::update` on the game thread.
- The patch is in `docs/dev/level-editor/spike/editor-spike.patch`. It is reference code, not for merging.

| Test | Result |
| --- | --- |
| Clone a Takobo (Octoomba) row from `SoramameZone`, place it 400 units in front of Mario, 250 up | Created in 0.24-0.30 ms (2 LiveActors: the body and its ice box). Fell under planet gravity onto the ground and walked with its AI. Drawn with its shadow |
| Clone a Coin and a SuperSpinDriver (Launch Star) | 0.08-0.15 ms and 0.34 ms. The Launch Star **crashed** in `BezierRail::BezierRail` when its `CommonPath_ID` was blanked; it worked keeping the rail link in the template's zone (required links, 2.11) |
| Freeze (skip the scene's nerves, as photo mode), move the new actors 400 up | Re-rendered at the new place while frozen (calcAnim still runs) |
| Move an existing actor while frozen | Moved, then **snapped back** on resume: that class re-derives its position (why the row, not the actor, is the truth) |
| Kill, then re-create from the row with `Obj_arg0 + 1` | Works; the tombstone stays dead |
| 50 Coins in one frame | 3.3-4.6 ms in total (0.06 ms each). Lists grew from load size, e.g. the coin draw buffer 73 → 115 |
| Objects whose archives were not resident: Teresa (Boo), Karon (Dry Bones) | Mounted on the game thread in 1.8 and 1.3 ms, created in 0.4-0.5 ms. The Boo chased Mario, the Dry Bones walked. They eventually killed the idle Mario (the smoke's FAIL on that run is a death, as designed) |
| Edit a row inside the loaded archive's memory, then a same-stage restart (miss path) | The edit **survived**: `Takobo l_id 66` placed at y −4788.1 instead of −5788.1. 3,871 ms from request to normal play |
| Stage smoke after all of the above (runs 3-6) | **PASS**: no crash, pause menu, walks and jumps all fine |

### 8.2 The hooks it needed (all `PETARI_NATIVE`, all small)

1. **Synthetic row zones**: `StageDataHolder::findPlacedStageDataHolder` asks a registry first (+5 lines).
2. **Late connect**: `NameObjExecuteHolder::nativeLateConnect(from)` grows category lists and draw executors and connects entries registered after load (+90 lines).
3. **Clipping view groups**: re-run `ViewGroupCtrl::initViewGroup` over the clipping lists (+5 lines).
4. **`FunctionAsyncExecutor::waitForEnd`**: return when no task was queued, because the main-thread path ran it inline (+6 lines).

The rest (row synthesis, the driver) is editor code. Logs and screenshots are
in the tech worker's thread storage (`spike/run1..run7`), not in the repo
(they show game imagery).

### 8.3 What it did not cover (next spikes)

- Gravity fields created live (expected to work: `registerGravity` appends).
- Areas, cameras and demos (designed as reload-pending anyway).
- Children (`ChildObjInfo`) of a synthesized parent (needs the synthetic zone page).
- A class sweep over all creators (S2).
- Memory growth over a long session (heap meter).

---

## 9. Decisions (the user, 2026-10-02)

The open questions are resolved; the user took every recommendation (SUMMARY.md has the UX ones):

1. **Patch mods by default** (no Nintendo data; built from each player's disc at start-up), with Full-archive export as an option. M1 writes Full archives first because today's loader reads them; the patch loader is M2.
2. **New galaxies are Petari-only** (the mod galaxy registry, 5.7). Edits to existing galaxies stay portable.
3. **Mesh gravity is deferred.** Terrain gets fitted game gravity shapes (6.3).
4. **No VoiceOver in v1** (ImGui).
5. **MIT/Apache libraries** for terrain (Manifold, cgltf, tinyobjloader, meshoptimizer) may be vendored when needed.
6. The editor is **off until switched on once** in the Home menu (Mods page, "Level editor"). With it off, the game is unchanged: no runtime hooks run, and no headroom is reserved at load.
7. **Formats and the catalog** are shared with Supernova (SMGCommunity, MIT) where practical: plain JSON, documented here.

## 10. Implementation notes (build phase)

- **Branches.** `level-editor` is the integration branch; each topic gets its own `editor/<topic>` branch (docs/dev/WORKTREES.md workflow).
- **Interface.** The UI <-> Runtime contract is `native/editor/include/petari/editor/edit_api.hpp`. It refines 2.10, as agreed with the UI worker:
  - The UI reads the EditModel directly from the overlay (main thread, never mutated there) and touches actors only in its seam hook.
  - There is no separate snapshot struct; `status()` carries the toolbar, meter and Checks data.
  - Selection and the editor camera are UI state.
  - Runtime owns Play and Reload, and calls back into the UI (`leaveEdit`, `resumeEdit`, `sceneRebuilt`).
- **Code layout.**
  - `native/editor/runtime/`: the EditModel, ops, SceneSync, reload, save.
  - `native/editor/ui/`: panels, camera, gizmo, picking.
  - Game-side hooks in `src/` stay `PETARI_NATIVE` and do nothing while the editor is off.
- **Engine hooks** (`editor/runtime-hooks`; `native/editor/include/petari/editor/runtime_hooks.hpp`, `src/Game/Editor/EditorScene.cpp`):
  - The spike's four hooks are productised. All are no-ops while the editor is off (`Hooks::enabled()`, set by the Mods-page switch or `PETARI_EDITOR=1`).
  - Headroom at load is `count + count/2 + 16` per category list and draw executor, with growth as the fallback. `DrawBuffer::init` now takes the executor's allocated size (identical when off).
  - The `waitForEnd` guard is unconditional: it only changes a path that read past the list.
  - `EditorScene::createFromRow` / `retire` are the game-side operations. They refuse past the actor and execute limits, minus a margin of 64.
  - `PetariEditor::` is routed to the host allocator (`native/src/host_allocation.cpp`), so the EditModel and session survive scene reloads. Game objects the editor creates still come from the scene heap.
  - Live check: `native/tools/editor_hooks_smoke.py` (Good Egg, editor off then on).
- **Reload and Play** (`editor/fast-reload`; S3):
  - `destroyScene` drops the archives the editor overrides (`EditorReload::dropOverriddenArchives`, with a new `ArchiveHolder::nativeRemove`).
  - `StageFileLoader::startLoadingStageFile` injects the model's archive bytes onto the scene heap, so nothing is read from disc.
  - The restart is a type-5 galaxy move from the restart point (or the mission start). For an editor reload it skips the lives counter, the shader gate and the circle wipe (a fade instead), and clears the already-done flags, so collected items come back.
  - Measured on Good Egg: **265-493 ms** from the request to normal play (the design target was ~2 s).
  - Play: if edits are pending, reload, then `leaveEdit` and teleport to the play marker; if not, `leaveEdit` and teleport at once. Reload from Edit ends with `resumeEdit`.
  - Overrides stay for the session, so a death or a Level Select warp into the galaxy also shows the edits.
  - The EditModel reads the unmodded disc as its originals (`DVD::mountInfo().filesDirectory`). A saved mod's edits then count as changes and stay in the next save.
  - Live check: `native/tools/editor_reload_smoke.py`. It enters Edit through the UI's Tab path, using `PETARI_MODS=LevelEditor`.
