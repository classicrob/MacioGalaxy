# M4: New galaxies as a user-facing flow (as built)

UX.md section 14 covers the sheet. TECH.md 5.7 and [new-galaxies.md](new-galaxies.md) cover the engine side: the registry, the guards and the save sidecar. This note says how the two meet, as built.

Owners:
- Terrain: the files, the registry, `Command::NewGalaxy` and `Command::SetGalaxyInfo`, the restart, and the Checks.
- UI: the sheet, the Missions panel, and the Level Select entry.
- Runtime: reviews the edit_api additions.

## The one hard constraint: a galaxy can't appear mid-session

The game learns its galaxies only once, at boot:
- `ScenarioDataParser` walks `/StageData` at boot.
- The save's galaxy storage is sized from the parser.
- The disc overlay is resolved and sorted when it mounts. Adding files afterwards renumbers FST entries under live `FileHolder`/`ArchiveHolder` caches.

So **Create writes the galaxy as a mod, switches the mod on, and restarts Petari straight into the galaxy, in Edit** (`new_galaxy_session.cpp`). The restart is `OSRestart`: an `execv` of the same process image, with the same arguments and the same PID.

Before restarting, the editor sets these variables:
- `PETARI_OPEN_STAGE=<G>`
- `PETARI_OPEN_TITLE`
- `PETARI_OPEN_FILE=<the open save file>`
- `PETARI_OPEN_EDIT=1`

`app_main` maps them to `PlayerLaunch::openStage/openFile/openTitle/openEdit` and clears them. The restarted game then:
- advances the strap by itself (`LogoScene`);
- skips the title and opens the same file without the file select (`FileSelector`, milestone `FileSelector.OpenFile`);
- enters the galaxy;
- has the UI enter Edit on `openEdit`.

A test fixture's restart (`PETARI_STAGE` set) goes in through the fixture's synthetic entry instead and ignores `openEdit`.

What is live and what restarts:

| Change | How |
| --- | --- |
| Edits, Power Stars, the start | Live, as in any galaxy |
| Galaxy name, mission names, music | Live: `Command::SetGalaxyInfo` rewrites `galaxy.json` and updates the registry in place (`ModGalaxy::update`; earlier message pointers stay valid). Music takes effect at the next reload or Play |
| Mission count | A restart: `SetGalaxyInfo` rewrites the Scenario archive, then restarts into the galaxy in Edit, the same way Create does. The parser and the save storage are fixed at boot, as above |

## Templates (Start from)

| Template | Files written |
| --- | --- |
| **Empty space** | The galaxy's own zone (an emptied `TransformationExGalaxy` stage), a start point and a sky. No planet, gravity or stars. The Checks list opens with "Mario starts in mid-air" and one "Mission N has no Power Star" per mission |
| **One planet** | Empty space plus a disc planet from `PlanetMapDataTable` under the start, with the gravity row the disc pairs with it (the nearest gravity within 100 units, in the first galaxy that places it). Plus a Power Star per mission |
| **Brush skeleton** | Empty space plus a checker box brush under the start (fitted gravity), plus a Power Star per mission |
| **Copy a galaxy** | A disc galaxy's own stage archive and its ScenarioData, under the new name. Its zones keep their disc names, so they stay shared and are not copied |

In every template:
- **Missions.** Mission k's Power Star (`Obj_arg0` = k) stands 900 units from the start, on the mission's layer. A single mission uses Common; with more, mission 1 is Layer A, and so on.
- **Sky.** `NewGalaxyRequest::sky` is one of `newGalaxyChoices().skies`: the game's Sky and ProjectionMapSky classes that the disc has. The row is copied as a disc galaxy places it and moved to the origin, in Common. Empty means Good Egg's sky; for Copy, empty keeps the source's sky. A chosen sky replaces the sky in the copy's own archive (a sky placed in a shared zone stays).
- **Lighting** falls back to the default area light.

## The pieces

**Library** (pure C++, `petari/editor/galaxy/new_galaxy.hpp`):
- `buildNewGalaxy(spec, readDisc, listDisc) -> {files, galaxyJson, log, error}`;
- `internalNameFor` and `checkInternalName`;
- `discGalaxies`, `planetNames` and `skyNames`;
- `galaxyJson`;
- `resizeScenario`: the first rows are kept, trailing ones dropped, and a new mission k copies mission 1's row with Power Star ID k and its own layer.

**edit_api.hpp:**
```cpp
struct NewGalaxyRequest { name, internalName, from {Empty, OnePlanet, Brushes, Copy}, planet, copyOf,
                          musicFrom, missions (1..8 names), sky, discardUnsaved };
struct NewGalaxyChoices { planets, galaxies, skies };    // newGalaxyChoices(), from the mounted disc
std::string newGalaxyInternalName(name);                 // "Pipe Dream" -> "PipeDreamGalaxy"
std::string checkNewGalaxyName(internalName);            // "" or why not (characters, disc names, save key, 64 galaxies)
Command::Kind::NewGalaxy                                 // command.newGalaxy

struct GalaxyInfo { internalName, mod (read only), name, missions, musicFrom, discardUnsaved };
std::optional<GalaxyInfo> modGalaxyInfo();               // the edited galaxy if it is a mod galaxy
std::string checkGalaxyInfo(const GalaxyInfo&);          // "" or why not; cheap enough to call as the user types
bool galaxyInfoNeedsRestart(const GalaxyInfo&);          // the mission count changes
Command::Kind::SetGalaxyInfo                             // command.galaxyInfo
```
- **Unsaved edits.** Both commands refuse while there are unsaved edits, unless `discardUnsaved` is set. The UI asks "save or discard" first. `SetGalaxyInfo` asks only when it would restart.
- **Results.** Results arrive as `status()` notices and errors ("New galaxy: ...", "Galaxy settings: ...").
- **Internal name.** It is fixed once created: the save, the files and the zones key on it.

**Files written** (`<user>/mods/<G>/`):
- `files/StageData/<G>.arc`;
- `files/StageData/<G>/<G>Scenario.arc`;
- `galaxy.json` (English names; other languages fall back to them);
- `mod.txt` with `editor=1`.

The mod is switched on in `mods.txt`. Brushes in the galaxy's own stage are baked by the loader when the editor is off (TECH.md 6.5). Editor sessions keep the rows live.

**Checks** (`galaxy_checks.cpp`; keys `galaxy-`, which are never baselined, so a new galaxy's list opens with them):
- "Mission N has no Power Star": no `PowerStar` with `Obj_arg0` = N in a layer that mission uses;
- "The galaxy has no start for Mario";
- "Mario starts in mid-air": a ray down 5,000 units from the start that hits neither a brush nor a placed planet's collision.

**Level Select:**
- The **Mod galaxies** group lists the registry's galaxies.
- A **New galaxy...** entry opens the sheet from the Home menu (UI).

**Share:** a mod with `galaxy.json` gets the note "This new galaxy plays only in Petari" (`Preview::newGalaxies`).

## Tests

- `editor_new_galaxy_tests`:
  - every template against the disc: stage, scenarios, layers, one sky, the registry entry;
  - `resizeScenario` growing and shrinking;
  - a `galaxyJson` round trip through `ModGalaxy::update`.
- `mod_galaxy_tests`: `update` in place; old message pointers stay valid; another mod can't take the galaxy.
- `native/tools/editor_new_galaxy_smoke.py` (`new_galaxy_test.cpp`):
  1. Create "Smoke Hop" (One planet, 2 missions) and restart into it. Check the stage, the registry and the hand-over; then the Checks (none, then "mid-air" once the start is moved up 50,000). Then rename it live, see a third mission refused over the unsaved move, and add it with discard across a second restart (3 missions).
  2. A player's restart, without the fixture: strap, title and file select skipped; in Edit by itself.
  3. "Smoke Box" (Brush skeleton).
  4. "Smoke Box" with the editor off: the loader bakes the brush, and the stage checks walk on it.
