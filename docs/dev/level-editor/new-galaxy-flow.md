# M4: New galaxies as a user-facing flow (design)

UX.md section 14 is the sheet; TECH.md 5.7 and [new-galaxies.md](new-galaxies.md) are the engine
side (registry, guards, sidecar). This note decides how the two meet. Owners: Terrain (the files,
the registry, `Command::NewGalaxy`, the relaunch, the Checks), UI (the sheet, the Home menu entry,
the planet palette), Runtime (reviewing the edit_api additions).

## The one hard constraint: a galaxy can't appear mid-session

The game learns its galaxies once:
- `ScenarioDataParser` walks `/StageData` at boot.
- The save's galaxy storage is sized from the parser.
- The disc overlay is resolved and sorted at mount: adding files renumbers FST entries under live
  `FileHolder`/`ArchiveHolder` caches.

So **Create writes the mod and relaunches Petari straight into the new galaxy, in Edit**. That is
`OSRestart`'s existing self-relaunch: same argv and environment, with `PETARI_OPEN_STAGE=<G>` and
`PETARI_OPEN_EDIT=1` added. It takes about 25 s with the warm caches. The sheet says
"Petari restarts into your galaxy". Everything after that is live:
- edits;
- mission names and music (the registry is updated in place);
- the mission count (the Scenario archive is re-injected and the parser's entry refreshed).

## Templates (Start from)

| Template | Files written |
| --- | --- |
| **Empty space** | The galaxy's own zone. Start point at the origin, no planet or gravity, the default camera. The Checks list opens with "Mission 1 has no Power Star" and "Mario starts in mid-air". |
| **One planet** | Empty space plus one disc planet from `PlanetMapDataTable` (the planet palette), placed under the start, with the gravity row the disc pairs it with (looked up in the galaxies that place it). |
| **Brush skeleton** | Empty space plus a checker box brush under the start (fitted gravity) and Power Star 1: today's `petari_terrain_bake --galaxy`, as a library call. |
| **Copy a galaxy** | A disc galaxy's stage and zone archives under the new name, with ScenarioData copied. The zones keep their disc names, so they stay shared and are not copied. |

The sky (`Sky` class rows from galaxies that use them) and the lighting presets are in step 2. In
v1 they're a sky row copied from the chosen galaxy; lighting falls back to the default area light.

## The pieces

**Library (pure C++, `petari/editor/galaxy/new_galaxy.hpp`, Terrain):**
`buildNewGalaxy(spec, readDisc) -> {files, galaxyJson, error}`, generalised from
`terrain_bake --galaxy`. It also drives the headless tools.

**edit_api (Runtime to review):**
```cpp
struct NewGalaxySpec {
    std::string name;            // "Pipe Dream" (English; other languages default to it)
    std::string internalName;    // empty: generated ("PipeDreamGalaxy"), checked against the disc and the save keys
    enum class From { Empty, OnePlanet, Brushes, Copy } from = From::OnePlanet;
    std::string planet;          // OnePlanet: a PlanetMapDataTable name
    std::string copyOf;          // Copy: a disc galaxy
    std::string musicFrom;       // a disc galaxy (TECH 5.7 borrowing)
    std::vector<std::string> missions;  // names; their count = the mission count (1..8)
    std::string mod;             // the mod it goes in (default: the internal name)
};
Command::Kind::NewGalaxy        // command.newGalaxy = spec; validates, writes, enables the mod, relaunches
struct NewGalaxyChoices { planets, galaxies (copy/music), skies };  // newGalaxyChoices(), for the sheet
std::string checkNewGalaxyName(const std::string& name);  // "" or why not (collision, characters, 64 galaxies)
```

**UI:** the three-step sheet:
- **File → New Galaxy** and Level Select → **New galaxy**;
- the planet strip, filled from `newGalaxyChoices().planets`. The Browser's thumbnail path needs a
  live actor, so v1 shows names with the shape group; thumbnails come once a planet is placed;
- after the relaunch, opening in Edit on `PETARI_OPEN_EDIT=1`: camera on the start, play marker on
  the start.

**Missions panel (live, Terrain + UI):**
- **Mission count:** add or remove a ScenarioData row, then re-inject the Scenario archive and
  refresh the parser.
- **Names:** a registry upsert.
- **Power Stars per mission:** a `PowerStar` row on the mission's layer, with `Obj_arg0` = the star
  id. The Browser already places PowerStar.
- **Checks** (Runtime's engine, provider from Terrain):
  - a mission with no star;
  - missions > 8, stars > 7;
  - 64 galaxies;
  - "Mario starts in mid-air" (no collision under the start within 5000 units, from
    `readKcl` + `raycast`).

**Level Select:** the "Mod galaxies" group already lists registry galaxies. After the relaunch the
new one is there.

**Share:** a mod with `galaxy.json` gets the line "This new galaxy plays only in Petari (the game
has no way to add galaxies without code patches)". It's a one-line check in the Share sheet:
`ModGalaxy::find` over the mod's galaxy.json.

## Order of work (Terrain)

1. The library and `Command::NewGalaxy` with the relaunch, verified by a smoke: create, relaunch, in
   Edit, a star placed, played, saved.
2. Live mission count and names, plus the Checks.
3. The Share note, Level Select's "New galaxy" entry (with UI), and the planet palette's gravity
   pairing.
