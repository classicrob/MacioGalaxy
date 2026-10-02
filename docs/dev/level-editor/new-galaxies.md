# New galaxies: crash sites and guards (spike S8)

Companion to [TECH.md](TECH.md) 5.7. This is every place that misbehaves when a galaxy exists in
`StageData/` (through a folder mod) but in none of the tables compiled into the executable:
`GalaxyID.bcsv`, `GameEventFlagTable`, `Message.arc` names, the BGM tables, the comet schedule.
The survey covered the code at `level-editor` `d1c73ab6e`. The spike's guards are on
`editor/galaxy-registry`.

Status:
- **Done**: the guard is implemented in the spike branch.
- **Proposed**: designed but not built yet.
- **Note**: no change is needed for v1.

## 0. How the two lookups fail

| Lookup | Behaviour for an unknown galaxy |
| --- | --- |
| `GameEventFlagTable::findFlag` | Returns null. `GameEventFlagChecker::canOn` and `isOn` dereference it (`GameEventFlagChecker.cpp:15-17, 68-70`) |
| `GalaxyID.bcsv` `findElement` | Returns `end()` (row 55), and `getValue` reads the **string pool as a row**: PowerStarNum 65, GrandGalaxyNo 115, and the `OpenCondition*` columns become wild string pointers |

`ScenarioDataParser` adds a galaxy to every per-galaxy list at boot as soon as its
`<G>Scenario.arc` exists (`ScenarioDataParser.cpp:159-189`, `GameDataGalaxyStorage.cpp:117-124`,
`SpinDriverPathStorage.cpp:255-260`). So one unknown galaxy **breaks the vanilla game for every
player**, not only inside that galaxy.

## 1. Crash sites, by game moment

| Moment | Site | Effect | Guard |
| --- | --- | --- | --- |
| Stage load | `PauseMenu::init` (`PauseMenu.cpp:87, 93`), via `GamePauseSequence::init` | Null galaxy/scenario name reaches `ReplaceTagProcessor::Replace` (`ReplaceTagProcessor.cpp:51`) | **Done**: message overlay in `MessageData::getMessageDirect` |
| Scenario starter | `ScenarioTitle::exeAppear` (`ScenarioTitle.cpp:34-35`) | Same null name | **Done** (message overlay) |
| Entry by name | `petariDirectStageMove` with a name the parser does not know | `makeAccessor` wraps a null `ScenarioData` | **Note**: only reached for typos (`PETARI_STAGE`) |
| Level Select, `--stage` | `kGalaxies` (`launch_stage.cpp:12-55`) | Mod galaxies are not listed (fails safe) | **Done**: the registry's galaxies follow the disc's in a "Mod galaxies" section, and `--stage` resolves them |
| Music | `AudStageBgmWrap::changeStageNameToSoundID`, `AudStageBgmTable::findDataElement`, `AudSceneMgr::findSceneSoundInfo` | Silence, and `cSceneInfo[0]` dummy wave sets | **Done**: each maps the mod galaxy to `galaxy.json`'s `music` galaxy |
| Pause menu star list (any galaxy, vanilla too) | `PowerStarList::getSortedList` (`PowerStarList.cpp:154`) → `isOn(name)` | Null flag dereference | **Done**: null-flag guard (registry galaxies count as open) |
| Pause menu star list | `makeGalaxyNameText` (`:546-550`) | Null name used as the `swprintf` format | **Done** (message overlay) |
| Pause menu star list | 75 rows (5 x 15, `:30-32, 147`), unchecked add | Overflow past 75 rows once galaxies and the race page exceed it | **Done**: at most 60 galaxies listed; the last page is kept for race times (the disc opens at most 42) |
| Star get, any galaxy (vanilla too) | `GameDataHolder::getGalaxyNumCanOpen` (`GameDataHolder.cpp:53-65`) → `canOn` | Null flag dereference, from `StageResultSequenceChecker` and `StageResultInformer` | **Done**: null-flag guard |
| Leaving with or without a star | `getIncludedGrandGalaxyId` → 115, then `setNextStageToAstroGalaxyOrDome` sets nothing, then `strcmp(nullptr)` in `isEqualStageStopCometScheduler` | Crash | **Done**: `JMapInfoIter::getValue` reads nothing past the table, so the default 0 (no dome) applies. See the S8 run for where the game goes |
| Result window | Null name arguments | Blank text (native `CustomTagProcessor` tolerates null) | **Done** (message overlay) |
| Coins, scenarios 9+ | `mMaxCoinNum[8]`, u8 star bits | Out-of-bounds write | **Proposed**: the registry and the editor cap missions at 8 |
| Star totals | `calcCurrentPowerStarNum`, `getPowerStarNumMax`, file select | Mod stars count toward 120, the Luigi event (>108) and the file-select total | **Done**: registry galaxies are excluded from both totals |
| Mission select (entered with scenario -1) | `ScenarioSelectLayout::appear :163`, `fadeInText :364` | Null names | **Done** (message overlay) |
| Mission select, `Comet` column | `GalaxyCometScheduler::findFromGalaxy` returns null (`:387-391`), dereferenced at `:645`, `:796` | Crash | **Done**: `getEncounterCometPowerStarId` returns -1 without a schedule (`:796` needs a scheduled comet, which mod galaxies never have); mod comets are not scheduled in v1 |
| Comet flags | `isOnGameEventFlagNormalComet` / `Coin100Comet` (`GameDataFunction.cpp:360-374`) | Null flag | **Done** (null-flag guard: off) |

## 2. Saving

| Record | Layout | Unknown galaxy |
| --- | --- | --- |
| `GALA` (`GameDataGalaxyStorage.cpp:214-275`) | Per galaxy, 20 bytes: u16 `getHashCode(name)` (h = c + 31 h, truncated to 16 bits), u8 flags, u8 visited, u16 coins[8] | **With the mod**: saved and loaded. **Without it**: `deserialize` returns 1 for the unknown hash, which `loadFromFileBinary` treats as success, so the record is skipped and **lost at the next save** |
| `SPN1` (`SpinDriverPathStorage.cpp:221-250, 285-313`) | u8 galaxy count; per galaxy u16 hash, size, u8 scenario count | Same: skipped, then lost. A changed scenario count resets the galaxy |
| `FLG1` (`GameEventFlagStorage.cpp:25-65`) | 15-bit hashes of the static flags only | Registry flags are not stored (the registry answers them) |

- 6 chunks per file in 0xF80 bytes. `BinaryDataChunkHolder::makeFileBinary` never checks the total size.
- **Hash collision.** A mod galaxy whose 16-bit key equals a disc galaxy's would share that galaxy's record. `findIndex(u16)` returns the first match.
  - **Done**: the parser rejects such a registry galaxy and logs which disc galaxy it collides with.
- **Done: the `GALA` sidecar.**
  - `GameDataAllGalaxyStorage::serialize` leaves registry galaxies out, so `GameData.bin` keeps the disc's shape.
  - `GameDataHolder::makeFileBinary` and `loadFromFileBinary` write and read their records in `<user>/mods-saves/<Mod>.json`, keyed by the user file's save name (`mario1`...). The format is in `petari/platform/mod_galaxy.hpp`.
  - Turning the mod off and on keeps the stars: `editor_galaxy_smoke.py` runs 3 and 4.
- **Proposed:** the same for `SPN1` (launch-star paths drawn after first use). Losing them only redraws the path.

## 3. Fixed counts

| Limit | Where | Guard |
| --- | --- | --- |
| 64 parsed galaxies | `FixedArray< ScenarioData*, 64 >`, unchecked add (`ScenarioDataParser.cpp:176-183`) | **Done**: refused past 64, with a log line |
| 64 galaxies with stars | `mSomeGalaxyStorages.init(64)` | **Done** (it follows from the parser cap) |
| 50 per-galaxy spin-driver records | `SpinDriverPathStorage.cpp:253`; overflowed **from the third mod galaxy** (48 on the disc) | **Done**: 64, the parser's cap |
| 16 spin drivers per scenario | `SpinDriverPathStorage.cpp:95` | **Proposed**: editor Check |
| 8 scenarios | u8 bits, `u16[8]` | **Proposed**: registry and editor cap |
| 7 stars in the scenario and pause panes | `ScenarioSelectLayout.cpp:122` | **Proposed**: editor Check |
| 48-byte stage names; `"Appear%s"` in 0x30 | `GameDataTemporaryInGalaxy`, `GameDataConst.cpp:60` | **Done**: registry names are at most 40 characters of `[A-Za-z0-9_]` |

## 4. Unknown galaxies without `galaxy.json`

A Whitehole-style mod can add a new galaxy without registry data. Before S8 that crashed the
vanilla game after any Power Star (section 1).

**Done.** `ScenarioDataParser` now parses only:
- GalaxyID's galaxies plus `FileSelect`, the only Scenario archive on the disc without a GalaxyID row; and
- registered mod galaxies.

It skips anything else with a `[mods]` log line. A vanilla disc parses exactly as before (checked:
48 Scenario archives on the disc, all in GalaxyID except `FileSelect`).

## 5. The registry (`native/platform/dvd/mod_galaxy.cpp`)

- At start-up the app reads `<mod>/galaxy.json` for every enabled folder mod (`platform_host.cpp`, after the overlay is resolved).
- Format: `format` 1, then `galaxies`, each with:
  - `name`
  - `title`, `shortTitle` and `missions` as `{ "en": ..., "fr": ..., "es": ... }`, English fallback
  - `music`
- The game's hooks are all `PETARI_NATIVE`, and they do nothing for disc galaxies:

| Hook | Answers |
| --- | --- |
| `MessageData::getMessageDirect` | `GalaxyName_<G>`, `GalaxyNameShort_<G>`, `ScenarioName_<G><n>` (host UTF-16, persistent buffers) |
| `GameEventFlagChecker::canOn/isOn` | A null flag is "on" for a registry galaxy's own name, "off" for anything else |
| `JMapInfoIter::getValue` | An iterator past its table reads nothing. This covers every `GalaxyID` lookup: open stars 0, grand galaxy 0, no `OpenCondition` |
| The three audio lookups | The borrowed galaxy's music and sound banks |
| `ScenarioDataParser` | Accept or refuse (section 4), the save-key collision, the 64 cap |
| `SpinDriverPathStorage` | 64 records |

## 6. S8 result

See TECH.md 7 (spike table) for the run's outcome, and `native/tools/editor_galaxy_smoke.py`
to repeat it.
