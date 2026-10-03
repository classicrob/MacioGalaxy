# Class sweep (spike S2): results

**PARTIAL: the sweep is still running.** Classes are measured in order of how often the disc places them, so the common ones come first; `untested` below are classes not run yet. Coverage: EggStarGalaxy 495/1183; OceanRingGalaxy 428/1183; KoopaBattleVs1Galaxy 0/1183.

Generated 2026-10-02 by `native/tools/class_sweep.py report` from `sweep.json`. Do not edit the tables by hand.

## Method

- **Driver:** `src/Game/Scene/ClassSweep.cpp` (PETARI_NATIVE, off unless `PETARI_CLASS_SWEEP_JOBS` is set), on the S1 spike hooks.
  For each class it mounts the game's archive list for the class (and waits), synthesizes a one-row ObjInfo from a row of the
  stage (the stage's own row of that class if any, else the first ObjInfo row renamed, args -1), sets every link field
  (CameraSetId, SW_*, MessageId, CastId, ViewGroupId, CommonPath_ID, ClippingGroupId, GroupId, DemoGroupId, MapParts_ID) to -1, creates
  the class 400 units in front of and 250 up from Mario, runs 24 frames, classifies, then kills what it made. Area-like creators get a 20-unit scale.
- **Harness:** `native/tools/class_sweep.py`. Batches of 25 classes per background-smoke process through `build/locked-app.sh`.
  A crash, assert or hang is attributed to the class that was in flight (log markers BEGIN/CREATED/RESULT/KILLED), the sweep
  resumes after it, and every failure from a multi-class batch is re-run alone in a fresh process (a class that passes alone is
  recorded ok with a note). The resume point is `results.jsonl`.
- **Bisect:** for failing classes, `keep` (the stage's own row's links) and then real ids harvested from the same fields of the stage's
  rows, reduced one field at a time while the class still passes.
- **Statuses:** see `statuses` in `sweep.json`. `invisible` and `dead` are heuristics from actor flags, not pixels.

## Caveats

- Classes are created once, in a stage that is already running. A pass says it did not crash in 24 frames, not that it behaves.
- Spawned objects with a template row keep the template's arguments; synthesized ones have all args -1 (defaults).
- Planet classes (PlanetMapCreator names, not in `cCreateTable`) are not covered.
- Areas and gravity are created tiny and left registered (no unregister), so later classes in a batch may be affected.

## Results per stage

| status | EggStarGalaxy | OceanRingGalaxy | KoopaBattleVs1Galaxy | any stage (worst) |
| --- | --- | --- | --- | --- |
| crash | 114 | 63 | 0 | 114 |
| assert | 4 | 1 | 0 | 4 |
| kills-player | 3 | 1 | 0 | 3 |
| invisible | 8 | 4 | 0 | 8 |
| dead | 34 | 26 | 0 | 34 |
| ok-nomodel | 27 | 27 | 0 | 27 |
| nonactor | 89 | 89 | 0 | 89 |
| ok | 216 | 217 | 0 | 216 |
| untested | 0 | 0 | 0 | 688 |

## Failing classes

Failures with empty links, worst stage first. `links` is the bisect verdict: the link fields that made the class work (value = the id used), `none` if nothing helped.

| class | stage: status | where | links that fix it |
| --- | --- | --- | --- |
| SwitchCube | EggStarGalaxy: crash | _ZN10SwitchArea8movementEv | - |
| SwitchSphere | EggStarGalaxy: crash | _ZN10SwitchArea8movementEv | - |
| SwitchCylinder | EggStarGalaxy: crash | _ZN10SwitchArea8movementEv | - |
| BigBubbleSwitchBox | EggStarGalaxy: crash | _ZN10SwitchArea8movementEv | - |
| BigBubbleSwitchCylinder | EggStarGalaxy: crash | _ZN10SwitchArea8movementEv | - |
| BigBubbleSwitchSphere | EggStarGalaxy: crash | _ZN10SwitchArea8movementEv | - |
| Mario | EggStarGalaxy: kills-player |  | - |
| Caretaker | EggStarGalaxy: crash | _ZN2MR22tryStartMoveTalkActionEP8NPCActor | - |
| Rabbit | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| TrickRabbit | EggStarGalaxy: crash | _ZN2MR23createRailGraphFromJMapERK12JMapInfoIter | - |
| TrickRabbitFreeRunCollect | EggStarGalaxy: crash | _ZN22TrickRabbitSnowCollect4initERK12JMapInfoIter | - |
| TrickRabbitGhost | EggStarGalaxy: crash | _ZN2MR23createRailGraphFromJMapERK12JMapInfoIter | - |
| HoneyBee | EggStarGalaxy: crash | _ZN2MR22tryStartMoveTalkActionEP8NPCActor | - |
| HoneyQueen | EggStarGalaxy: crash | _ZN10HoneyQueen4initERK12JMapInfoIter | - |
| Penguin | EggStarGalaxy: crash | _ZN2MR22tryStartMoveTalkActionEP8NPCActor | - |
| PenguinCoach | EggStarGalaxy: crash | _ZN2MR35tryTalkNearPlayerAndStartTalkActionEP8NPCActor | - |
| PenguinMaster | EggStarGalaxy: crash | _ZN2MR35tryTalkNearPlayerAndStartTalkActionEP8NPCActor | - |
| PenguinRacer | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| PenguinRacerLeader | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| PenguinSkater | EggStarGalaxy: crash | _ZN13PenguinSkater4initERK12JMapInfoIter | - |
| PenguinStudent | EggStarGalaxy: crash | _ZN14PenguinStudent18initAfterPlacementEv | - |
| SignBoard | EggStarGalaxy: crash | _ZN12TalkDirector7requestEP15TalkMessageCtrlb | - |
| Syati | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| TicoComet | EggStarGalaxy: crash | _ZN9TicoComet4initERK12JMapInfoIter | - |
| DemoRabbit | EggStarGalaxy: crash | _ZN2MR11forwardNodeEP15TalkMessageCtrl | - |
| TicoGalaxy | EggStarGalaxy: crash | _ZN18GameEventFlagTable38getStarPieceNumToOpenExclamationGalaxyEPKc | - |
| TicoRail | EggStarGalaxy: crash | _ZN2MR13isEqualStringEPKcS1_ | - |
| TicoDomeLecture | EggStarGalaxy: crash | _ZN2MR26tryRegisterDemoActionNerveEPK9LiveActorPK5NervePKc | - |
| RosettaReading | EggStarGalaxy: crash | _ZN20DemoExecutorFunction33tryCreateDemoTalkAnimCtrlForSceneEP12DemoEx | - |
| LuigiNPC | EggStarGalaxy: crash | _ZN15TalkMessageCtrl18registerBranchFuncERK19TalkMessageFuncBase | - |
| LuigiEvent | EggStarGalaxy: crash | _ZN15TalkMessageCtrl18registerBranchFuncERK19TalkMessageFuncBase | - |
| TeresaRacer | EggStarGalaxy: crash | _ZN15TalkMessageCtrl18registerBranchFuncERK19TalkMessageFuncBase | - |
| NPCSupportRail | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| GhostPlayer | EggStarGalaxy: assert | _ZN12PetariNative24prepareAllocationSymbolsEv.cold.1 | - |
| Butler | EggStarGalaxy: crash | _ZN6Butler18initForAstroGalaxyERK12JMapInfoIter | - |
| Creeper | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| FluffWind | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| TamakoroWithTutorial | EggStarGalaxy: crash | _ZN2MR21getJMapInfoArg0NoInitERK12JMapInfoIterPi | - |
| Plant | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| TreeCube | EggStarGalaxy: crash | _ZN16KCollisionServer4initEPvPKv | - |
| WaterRoad | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SwitchBox | EggStarGalaxy: crash | _ZN9SwitchBox6appearEv | - |
| NormalMapTestObj | EggStarGalaxy: assert | _ZN12PetariNative24prepareAllocationSymbolsEv.cold.1 | - |
| Balloonfish | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| SuperSpinDriver | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SuperSpinDriverGreen | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SuperSpinDriverPink | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| Kabokuri | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| EyeBeamer | EggStarGalaxy: crash | _ZN9EyeBeamer4initERK12JMapInfoIter | - |
| HomingKiller | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| FireBubble | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| Pukupuku | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| TeresaChief | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN20PowerStarEventKeeper22requestAppearPowerStarEPKciPKN9JGeometry5TV | - |
| Torpedo | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| Onimasu | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| OnimasuPivot | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| KirairaRail | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| NokonokoLand | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| Snakehead | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SnakeheadSmall | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| Kanina | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| KaninaRed | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| Mogucchi | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| KoopaJrShip | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| Hanachan | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| StinkBugParent | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN20PowerStarEventKeeper22requestAppearPowerStarEPKciPKN9JGeometry5TV | - |
| BombBird | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| MechanicKoopaMini | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| DharmaSambo | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN11DharmaSambo17calcAndSetBaseMtxEv | - |
| MagnumKiller | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| BossBegoman | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN20PowerStarEventKeeper22requestAppearPowerStarEPKciPKN9JGeometry5TV | - |
| BossStinkBug | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| DinoPackun | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| DinoPackunVs2 | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| Dodoryu | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| KoopaVs1 | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN14CameraManEvent5startEiPKcRK15CameraTargetArgi | - |
| KoopaVs2 | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN2MR35tryStartTimeKeepDemoMarioPuppetableEP9LiveActorPKcS3_ | - |
| KoopaVs3 | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN20DemoExecutorFunction33tryCreateDemoTalkAnimCtrlForActorEP12DemoEx | - |
| Polta | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN20PowerStarEventKeeper22requestAppearPowerStarEPKciPKN9JGeometry5TV | - |
| SkeletalFishBoss | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN16SkeletalFishBoss18initAfterPlacementEv | - |
| SkeletalFishBaby | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SkeletalFishBabyRail | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SkeletalFishBossRail | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| TombSpider | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN14CameraManEvent5startEiPKcRK15CameraTargetArgi | - |
| BossKameck | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| BossKameck2 | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| TripodBoss | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN9PowerStar10setupColorEP9LiveActorPK7NameObji | - |
| Tripod2Boss | EggStarGalaxy: assert | _ZN12PetariNative24prepareAllocationSymbolsEv.cold.1 | - |
| TripodBossBaseJointPosition | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN14TripodBossBone19setAttachBaseMatrixERKN9JGeometry10TPosition3INS0 | - |
| TripodBossCoin | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN2MR19isStartedTripodBossEv | - |
| SpiderAttachPoint | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN12SlingShooter18initAfterPlacementEv | - |
| SpiderCoin | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZNK13NrvSpiderCoin17SpiderCoinNrvWait7executeEP5Spine | - |
| ArrowSwitch | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| ClipAreaDropLaser | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| JumpHole | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| DragonHeadFlower | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SphereRailDash | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| BallRail | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| RockCreator | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN2MR13isEqualStringEPKcS1_ | - |
| Tsukidashikun | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13Tsukidashikun4initERK12JMapInfoIter | - |
| DesertMovingLand | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN16DesertMovingLand7exeWaitEv | - |
| DesertMovingLandSmall | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN16DesertMovingLand7exeWaitEv | - |
| NoteFairy | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| ElectricRail | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| ElectricRailMoving | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| WanwanRolling | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| WanwanRollingMini | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| WanwanRollingGold | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| BlackHole | EggStarGalaxy: kills-player; OceanRingGalaxy: kills-player |  | - |
| BlackHoleCube | EggStarGalaxy: kills-player |  | - |
| CollapsePlane | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN13CollapsePlane17calcAndSetBaseMtxEv | - |
| LavaProminence | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN2MR13isEqualStringEPKcS1_ | - |
| LavaProminenceWithoutShadow | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| LavaHomeVolcanoFlow | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZNK22NrvLavaHomeVolcanoFlow12HostTypeDone7executeEP5Spine | - |
| LavaShellTower | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN14LavaShellTower4initERK12JMapInfoIter | - |
| MeteorStrike | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| MeteorStrikeEnvironment | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| MeteorCannon | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN2MR13isEqualStringEPKcS1_ | - |
| TemplateStageGeometry | EggStarGalaxy: assert; OceanRingGalaxy: assert | _ZN12PetariNative24prepareAllocationSymbolsEv.cold.1 | - |
| IceMerameraKing | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN15IceMerameraKing4initERK12JMapInfoIter | - |
| OceanSmallTurtle | EggStarGalaxy: crash; OceanRingGalaxy: crash | _ZN2MR25initDefaultPosForMercatorEP9LiveActorRK12JMapInfoIterb | - |

## Classes that need archives mounted

231 classes had an archive that was not resident in the stage (listed per class in `sweep.json`: `archives`, per stage `nonresident`).

