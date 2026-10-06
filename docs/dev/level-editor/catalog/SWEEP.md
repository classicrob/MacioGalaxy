# Class sweep (spike S2): results

**PARTIAL: the sweep is still running.** Classes are measured in order of how often the disc places them, so the common ones come first; `untested` below are classes not run yet. Coverage: EggStarGalaxy 901/1183; OceanRingGalaxy 428/1183; KoopaBattleVs1Galaxy 0/1183.

Generated 2026-10-02 by `native/tools/class_sweep.py report` from `sweep.json`. Do not edit the tables by hand.

## Method

- **Driver:** `src/Game/Scene/ClassSweep.cpp` (PETARI_NATIVE, off unless `PETARI_CLASS_SWEEP_JOBS` is set), on the editor runtime's hooks (registerRows, nativeLateConnect).
  For each class it mounts the game's archive list for the class (and waits), synthesizes a one-row ObjInfo from a row of the
  stage (the stage's own row of that class if any, else the first ObjInfo row renamed, args -1), sets every link field
  (CameraSetId, SW_*, MessageId, CastId, ViewGroupId, CommonPath_ID, ClippingGroupId, GroupId, DemoGroupId, MapParts_ID) to -1, creates
  the class 400 units in front of and 250 up from Mario, runs 24 frames, classifies, then kills what it made. Area-like creators get a 20-unit scale.
- **Harness:** `native/tools/class_sweep.py`. Batches of 25 classes per background-smoke process through `build/locked-app.sh`.
  A crash, assert or hang is attributed to the class that was in flight (log markers BEGIN/CREATED/RESULT/KILLED), the sweep
  resumes after it, and one in eight failures from a multi-class batch is re-run alone in a fresh process (a class that passes alone
  is recorded ok with a note; the rest keep the stack of the class in flight). Classes run in order of how often the disc places them
  (`placed_on_disc`). The resume point is `results.jsonl`. On the last stage, classes that failed on another stage are not run: their
  record is copied and marked `inferred_from`.
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
| crash | 195 | 63 | 0 | 195 |
| assert | 8 | 1 | 0 | 8 |
| kills-player | 4 | 1 | 0 | 4 |
| invisible | 10 | 4 | 0 | 10 |
| dead | 43 | 26 | 0 | 43 |
| ok-nomodel | 91 | 27 | 0 | 91 |
| nonactor | 93 | 89 | 0 | 93 |
| ok | 457 | 217 | 0 | 457 |
| untested | 0 | 0 | 0 | 282 |

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
| ItemAppearStoneSpinDriver | EggStarGalaxy: crash | _ZNK18NrvItemAppearStone12HostTypeWait7executeEP5Spine | - |
| FlipPanelObserver | EggStarGalaxy: crash | _ZN17FlipPanelObserver7exeWaitEv | - |
| ChooChooTrain | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| AstroDomeBlueStar | EggStarGalaxy: crash | _ZN2MR28tryRegisterDemoActionFunctorEPK9LiveActorRKNS_11FunctorBaseEPK | - |
| TwisterSan | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| TwisterSanSlim | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SunakazeKun | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SandCapsulePressGround | EggStarGalaxy: crash | _ZNK25NrvSandCapsulePressGround32SandCapsulePressGroundNrvWaitTop7exec | - |
| RosettaChair | EggStarGalaxy: crash | _ZN12RosettaChair4initERK12JMapInfoIter | - |
| KoopaRestarterVs3 | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| KoopaPowerUpSwitch | EggStarGalaxy: crash | _ZN13KoopaFunction26registerKoopaPowerUpSwitchEP9LiveActor | - |
| KoopaSwitchKeeper | EggStarGalaxy: crash | _ZN13KoopaFunction25registerKoopaSwitchKeeperEP9LiveActor | - |
| KoopaViewSwitchKeeper | EggStarGalaxy: crash | _ZN13KoopaFunction29registerKoopaViewSwitchKeeperEP9LiveActor | - |
| KoopaBattleMapStair | EggStarGalaxy: crash | _ZN13KoopaFunction22registerBattleMapStairEP19KoopaBattleMapStair | - |
| KoopaBattleMapStairBig | EggStarGalaxy: crash | _ZN13KoopaFunction22registerBattleMapStairEP19KoopaBattleMapStair | - |
| KoopaBattleMapStairTurn | EggStarGalaxy: crash | _ZN13KoopaFunction22registerBattleMapStairEP19KoopaBattleMapStair | - |
| AstroDome | EggStarGalaxy: assert | _ZN12PetariNative24prepareAllocationSymbolsEv.cold.1 | - |
| AstroDomeEntrance | EggStarGalaxy: crash | _ZN16KCollisionServer4initEPvPKv | - |
| AstroDomeSky | EggStarGalaxy: assert | _ZN12PetariNative24prepareAllocationSymbolsEv.cold.1 | - |
| RailCoin | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| PurpleRailCoin | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| PurpleCoinStarter | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| BlueChip | EggStarGalaxy: crash | _ZN8ChipBase18initAfterPlacementEv | - |
| StarPieceFlow | EggStarGalaxy: crash | _ZNK17NrvStarPieceGroup15HostTypeNrvFlow7executeEP5Spine | - |
| StarPieceFollowGroup | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| OceanRing | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| OceanRingAndFlag | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| FishGroupA | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| FishGroupB | EggStarGalaxy: crash | _ZN2MR13isEqualStringEPKcS1_ | - |
| FishGroupC | EggStarGalaxy: crash | _ZN2MR13isEqualStringEPKcS1_ | - |
| FishGroupD | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| FishGroupE | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| FishGroupF | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| SeaGullGroup | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| HipDropSwitch | EggStarGalaxy: crash | _ZN13HipDropSwitch6exeOffEv | - |
| HipDropTimerSwitch | EggStarGalaxy: crash | _ZN18HipDropTimerSwitch6exeOffEv | - |
| KeySwitch | EggStarGalaxy: crash | _ZN9KeySwitch4killEv | - |
| FallingSmallRock | EggStarGalaxy: crash | _ZNK19NrvFallingSmallRock12HostTypeWait7executeEP5Spine | - |
| SandUpDownKGBaseEffect | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| SandUpDownTowerTopEffect | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| UFOSandObstacleAEffect | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| UFOSandObstacleBEffect | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| UFOSandObstacleCEffect | EggStarGalaxy: crash | _ZN13SwitchWatcher8movementEv | - |
| Steam | EggStarGalaxy: kills-player |  | - |
| GlobalWireGravity | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| MiniEggStarGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniHoneyBeeKingdomGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniFlipPanelExGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniSurfingLv1Galaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniTriLegLv1Galaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniStarDustGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniTamakoroExLv1Galaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniBattleShipGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniBreakDownPlanetGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniKoopaBattleVs1Galaxy | EggStarGalaxy: assert | _ZN12PetariNative24prepareAllocationSymbolsEv.cold.1 | - |
| MiniHeavenlyBeachGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniCubeBubbleExLv1Galaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniPhantomGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniOceanFloaterLandGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniKoopaJrShipLv1Galaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniCosmosGardenGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniIceVolcanoGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniHoneyBeeExGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniSandClockGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniKoopaBattleVs2Galaxy | EggStarGalaxy: assert | _ZN12PetariNative24prepareAllocationSymbolsEv.cold.1 | - |
| MiniReverseKingdomGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniOceanRingGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniFactoryGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniSkullSharkGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniFloaterOtaKingGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniOceanPhantomCaveGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniCannonFleetGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniDarkRoomGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| MiniHellProminenceGalaxy | EggStarGalaxy: crash | _ZN23MiniatureGalaxyFunction31registerMiniatureGalaxyToHolderEP9LiveAc | - |
| ClipFieldSwitch | EggStarGalaxy: crash | _ZN2MR12isInClipAreaERKN9JGeometry5TVec3IfEEf | - |
| GroupSwitchWatcher | EggStarGalaxy: crash | _ZN18GroupSwitchWatcher7controlEv | - |
| RaceRail | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| TimerSwitch | EggStarGalaxy: crash | _ZN11TimerSwitch7controlEv | - |
| SwitchSynchronizerReverse | EggStarGalaxy: crash | _ZN18SwitchSynchronizer8movementEv | - |
| DemoGroup | EggStarGalaxy: crash | _ZN16DemoCameraKeeperC2EP12DemoExecutorRK12JMapInfoIter | - |
| AstroDomeDemoStarter | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| AstroDomeGalaxySelector | EggStarGalaxy: crash | _ZN23AstroDomeGalaxySelector4initERK12JMapInfoIter | - |
| StarReturnDemoStarter | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| GrandStarReturnDemoStarter | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| ScenarioStarter | EggStarGalaxy: crash | _ZN10BezierRailC2ERK12JMapInfoIterPK8JMapInfo | - |
| FileSelector | EggStarGalaxy: crash | _ZN14CameraManEvent5startEiPKcRK15CameraTargetArgi | - |

## Classes that need archives mounted

470 classes had an archive that was not resident in the stage (listed per class in `sweep.json`: `archives`, per stage `nonresident`).

