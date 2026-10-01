# Cappy mod (design)

An optional mod, **`Cappy`** (off by default), that gives Mario Super Mario
Odyssey's cap throw, on top of the Odyssey movement mod
(docs/dev/ODYSSEY_MOVEMENT.md). The target is SMO's signature air loop: jump,
throw the cap in the air (a small hop), ground pound, dive into the hovering cap,
bounce off it, and dive again. With the mod off the game is unchanged.

Status (2026-10-01): throw, flight, hover, return, the visual and the three
cap bounces are implemented and measured (see "Measured"); the bounce contact
was reworked after the user's playtest ("Bounce contact, after the playtest").
Cappy hits enemies (verified on Good Egg) and collects Star Bits (verified) and
coins (same path, not yet seen in game). Enemy hits and
collection are not done yet. Captures are out of scope.

## Sources

Letters as in the movement doc: **D** = OdysseyDecomp (`PlayerConst.cpp`,
`PlayerCapActionHistory.cpp`), **C** = community measurement (smo.wiki
[Cap throw](https://smo.wiki/Cap_throw) and [Cap bounce](https://smo.wiki/Cap_bounce),
and the "Odyssey Movement Data v2" sheet, tabs "MCCT Data (Old)" and "Cap Throws
and Roll Cancels"), **I** = our own choice where nothing is published.

SMO's Cappy actor (`HackCap`, 189 functions) is not decompiled, and the values
in its ActorParam file (`HackCapThrowParam`: throw height, speed, brake time,
reach, endpoint stop time, return strength and speed) are not published
anywhere. The placeholder stubs in closed decomp PRs #1290/#1292/#1295 are not
real data and are not used. Cappy's flight therefore comes from the sheet's
measured trajectories; Mario's side (throw frames, air hop, bounces) is in
`PlayerConst` and matches the measurements.

## Numbers

Scale 1.0, as in the movement mod (both games' Mario is about 160 units tall
and runs at 60 fps). All heights are along -gravity, all horizontal distances
across it.

### Mario's throw

| What | Value | Source |
|---|---|---|
| Throw animation before Cappy leaves | ground 12 frames, air 8 | `SpinCapThrowFrame`, `SpinCapThrowFrameAir` (D); fully thrown at 34 / 30 = 12/8 + 21 + 1 (C) |
| Press buffer | 10 frames | `PreInputFrameCapThrow` (D) |
| Ground throw | speed ×0.95 a frame for 15 frames | `SpinBrakeFrame` 15, `SpinBrakeRate` 0.95 (D = C "5%/f for 15 frames") |
| Air throw hop | up 6, gravity 0.3; horizontal speed capped at 7, side accel 0.3 (7 u/f in 24 frames); falls normally (gravity 1.5, cap 11) after 24 frames | `SpinAirJumpPower`, `GravitySpinAir`, `SpinAirSpeedMax` (D); measured 5.7 initial, peak 57 u at frame 19, falling after 24 f (C) |
| Repeat air throws | no new hop while Mario is above the height of the last air throw (the up speed is not raised) | `PlayerCapActionHistory::recordLimitHeight` (D, structure only), I |
| Jump / ground pound after a throw | ground 13 frames, air 9 | sheet "jump/GP lockout" (C) |

### Cappy's flight

The sheet measures the motion-control throw frame by frame; the button throw
takes the same 21 frames once Cappy leaves (fully thrown 34 vs 29 frames from the
press, the difference being Mario's wind-up), so its trajectory is used for both.

| What | Value | Source |
|---|---|---|
| First frame | 182 ahead of Mario, 70 up | C; drawn coming out of his hand (50) and on the measured line from frame 4 (I) |
| Cruise | 32 u/f on frames 2–12 | C |
| Brake | −3.2 u/f² on frames 13–21 (28.8 … 3.2), stop at 678 | C |
| Hover, button held | until frame 122 after leaving ("at max length f22–122") | C |
| Hover, button released | 10 frames, then return | I (unpublished endpoint stop time) |
| Return | accelerate 2 u/f² to 40 u/f toward Mario's head, caught within 80 | I (unpublished return strength / speed) |
| Size | radius 50 (Cappy rests 50 above the floor) | C |
| Walls | a map polygon between this frame's and the next position stops the flight there (Cappy hovers at the wall) | C (SMO stops at walls within 30° of perpendicular; any wall here, v1) |
| Path | a straight line along Mario's facing across gravity at the press (the dive's direction; Galaxy's throw spin must not turn it), from where he is at the release, fixed in the world | C |
| Drawn size | Mario's cap, growing 1.8x over 4 frames once fully thrown, shrinking on the way back | C ("he visibly grows"), I (the factor) |

### Cap bounce

| What | Value | Source |
|---|---|---|
| Bounce from a dive | up 22, gravity 1.0, horizontal min(v, 16), cap 16 | `CapHeadSpringJumpPowerHigh`, `CapHeadSpringSpeedMax` (D); 231 u in 21 f (C) |
| Regular bounce (jump or fall into Cappy) | up 25, gravity 1.0, horizontal min(v, 24), cap max(v, 14) | `TrampleHighJumpPower`/`Gravity` (D); 300 u in 24 f (C) |
| Bounce from a ground pound | up 35, gravity 1.5, horizontal cap 14 | `TrampleHipDropJumpPower`/`Gravity` (D); 390.5 u (C) |
| Contact | Mario's head or body sphere (r 40 at 110 up, r 50 at 75) touches a sphere of r 70 at Cappy (SMO: 100, for a much bigger Cappy); or his foot (r 40 at 40) touches that sphere around Cappy's catch point (20 above Cappy), at most 70 above it and at least 20° above it. A ground-pound bounce needs the foot rule | C, scaled (I): see below |
| Conditions | Cappy hovering (fully thrown), Mario in the air | C, relaxed (below) |
| One per airtime | refreshed on landing; a wall jump removes it until the next landing | C; `clearLandLimit` sets `mIsCapJumpPossible` (D) |
| After a bounce | Cappy returns to Mario | C |

A ground throw followed by a jump into the hovering cap is a regular bounce.

**The hold rule, relaxed (I).** smo.wiki says the throw button must be held for a
bounce (released, Cappy just returns). Played on a keyboard that makes the loop
finicky: the R that starts the dive is tapped, and the dive reaches Cappy about
22 frames later. So here holding only *extends* the hover (to frame 122); a
tapped throw still hovers 10 frames and can be bounced on then; and a throw
press while Cappy is out (the dive's R) counts as holding for 30 frames
(`HoldLatchFrames`). The harness's loop uses 6-frame taps throughout and
bounces every run.

After a bounce the air throw's hop limit is cleared (I: a bounce is a new air
action), so a throw after the bounce hops again. The bounce also gives back the
jump-press dive ("Jump in the air dives", once per airtime otherwise), so the
loop is jump, R, jump (dive), bounce, jump (dive). Any dive started while Cappy
is out (jump press or ground pound + Spin) keeps him there as the throw key's
press does (the latch).

### Bounce contact, after the playtest

The user found the loop janky, "a lot of air between Cappy and Mario in the
bounce". Their recording (playtest-recordings/all-missions-20261001-152029.csv,
frames 15677-15780: a standstill jump, the air throw, a jump-press dive, the
dive bounce 22 frames later) and the harness trace showed why: the bounce was
SMO's geometry on a cap a quarter of Cappy's size, and the foot rule as written
had no distance bound, so it fired with Cappy 231 across and 85 below Mario's
feet (a body length away; the drawn cap was where the logic had it, within one
frame). Changes:

- the contact sphere is 70, and the foot rule also needs the foot to touch it
  (cappy_tests: the 231/85 case is no contact now);
- the hovering cap grows 1.8x, as SMO's Cappy does when fully thrown;
- on the contact frame Cappy moves to touch the sphere that met him (head, body
  or feet; 32-36 units in the harness), then flies back and shrinks on the way;
- the line's direction is taken at the press;
- Galaxy's spin trail and sparkles (the throw animations' `スピンライト`) are
  stopped for a throw.

Before/after XFB sheet: build/cappy-runs/bounce-before-after.png (local).

## How it hooks into Galaxy

- **Mod toggle `Cappy`** (`Mods::Mod::Cappy`, `mods.txt` `Cappy=on`), off by
  default. It needs `OdysseyMovement` (the bounce loop is built on the mod's dive
  and air model): with Cappy on and movement off, Cappy is inert and the Mods
  page says so ("Cappy: On (needs Odyssey movement)"). Only normal Mario throws
  (player mode 0, on foot or in the air, not swimming, riding, hanging or in a
  cutscene): the same places Galaxy lets Mario spin (`MarioActor::updateSwingAction`).
- **Input: a new action `ModCappyThrow`**, key **R** (free; next to the movement
  keys), controller **RB**. RB is also Galaxy's second Spin button (X spins too):
  while Cappy is active, an input bound to both Spin and Cap throw only throws;
  with the mod off RB spins as before. Rebindable (`ModCappyThrow=Key:R,Pad:RightShoulder`).
  Like SMO's Y, the throw key does the mod's Y moves where SMO does: during a
  ground pound it dives, while crouching it rolls, while rolling it boosts.
  Galaxy's Star Spin (F) is unchanged.
- **The model** (`native/include/petari/cappy.hpp`, header-only, no game types):
  Cappy's state machine (idle, wind-up, flying, hovering, returning) with the
  distances above, the bounce contact test and the bounce availability rule.
  Unit tests in `native/tests/cappy_tests.cpp`. The new air moves (air throw hop,
  three bounces) are added to `odyssey_move.hpp`'s `Air` model, so they share the
  movement mod's gravity-relative integration.
- **Game side** (`src/Game/Player/MarioCappy.cpp`, native only): creates the cap
  models at `MarioActor::initParts` when Cappy is on, reads the throw input in
  `MarioActor::controlMain`, starts throws in `updateSwingAction`, steps Cappy
  after `Mario::update`, and asks `MarioJump.cpp` to start the hop or a bounce
  (those touch the movement mod's air state, which lives there).
- **Visual.** Galaxy has a separate cap: `MarioHair.arc` (stationed, always
  loaded), made for the star-return cutscene where Mario takes his cap off, holds
  a cap model (`CapRoot`, material `Cap_v`) and hair (`Hair_v`, `Hair2_v`); the
  cutscene hides Mario's own cap with `MR::hidePlayerJoint("Cap0")`. The mod uses
  two `MarioParts` of it: the flying cap (hair materials hidden), spinning about
  gravity's up axis, and the hair on Mario's head (cap material hidden), fixed to
  his `Cap0` joint while Cappy is out, with `Cap0` hidden. Luigi has no hair model:
  he keeps his cap and throws Mario's (a known limit).
- **Created only with the mod on at stage load.** The two models are created
  with Mario when the stage loads, so turning Cappy on takes effect at the next
  stage load (changing the actor list mid-scene is not safe). With the mod off
  nothing is created and nothing changes.
- **Trace.** `PETARI_CAPPY_TRACE=1` logs Cappy's state, position and distance
  each frame, and Mario's bounce eligibility (available, held, contact).

## Measured

`PETARI_SMOKE=movement PETARI_MOVEMENT_ONLY=cappy PETARI_MODS=OdysseyMovement,Cappy`
on the observatory fixture (`PETARI_CAPPY_TRACE=1` for the per-frame trace):

- **cappy loop** (running jump, throw 6 frames in, ground pound at 18, dive with
  the throw key at 22, all 6-frame taps): Cappy leaves 8 frames after the press,
  is 678 out on his 21st frame and hovers; the dive arcs over him and comes down
  onto him, and the dive cap bounce comes on frame 50 (foot contact, 104 from
  Cappy; frame 44, 231 across, before the contact rework).
- **cappy standstill loop** (the user's inputs: a standstill jump held 24
  frames, R at 19, Jump at 40 for the dive, Jump 12 frames after the bounce):
  the dive meets Cappy at Mario's body (118 from him) on frame 69, the bounce
  rises, and the second jump-press dive follows. The ground pound and
  dive after the bounce work; the bounce then rises 231 (from the model; the
  harness's "rose 322" adds the second dive's rise). Cappy returns and is caught.
- **cappy ground bounce** (a running throw held, the run on, a jump 2 frames
  tapped 510 past the throw): the jump meets Cappy 124 up and the regular bounce
  rises 300.000. An earlier run whose jump tap was missed ran off the terrace
  edge under Cappy and bounced from the fall: also a regular bounce, as in SMO.
- XFB dumps (`PETARI_XFB_DUMP_LABELS="cappy throw,cappy dive,cappy bounce,cappy
  dive 2"`): the cap leaves Mario's head when it is thrown, and he shows his hair
  (MarioHair on `Cap0`) until it is caught. The red cap spins along the line,
  hovers, and flies back; the bounce plays Galaxy's mid-air somersault.
- Movement baseline (`native/tools/movement_baseline.py`): PASS with mods off,
  with OdysseyMovement on, and with OdysseyMovement and Cappy on.

## Plan

1. This note.
2. Throw, flight, hover and return, with the visual: model tests; then XFB
   dumps of a ground throw and an air throw on the observatory.
3. Cap bounce and the dive loop: a `cappy loop` harness task in
   `native/app/smoke_movement.cpp` (jump, throw, ground pound, dive with the
   throw held, bounce, ground pound, dive) and its XFB sequence; model tests for
   the three bounces' heights (231 / 300 / 390.5).
4. Nice to have: Cappy's hit sensor attacks enemies the way the Star Spin does
   (Galaxy's spin-attack message), and collects coins and Star Bits it touches.

The movement baseline (`native/tools/movement_baseline.py`) must still PASS with
all mods off, and with OdysseyMovement on and Cappy off.
