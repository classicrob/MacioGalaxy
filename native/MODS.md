# Native mods

Optional gameplay helpers. Every mod is **off** unless you turn it on, so an
unmodified game and every test run behave exactly as before.

| Mod (`mods.txt` name) | Button | What it does |
| --- | --- | --- |
| Collect visible Star Bits (`CollectStarBits`) | G, controller L3 (left stick click) | Every Star Bit on screen flies to Mario, as if the pointer had touched each one |
| Fire a Star Bit at the nearest enemy (`ShootEnemy`) | V (no controller default) | Shoots one Star Bit at the nearest enemy on screen |
| Odyssey movement (`OdysseyMovement`) | none (a mode; it uses the normal Jump, Crouch and Spin buttons) | Mario moves with Super Mario Odyssey's physics and moveset: see "Odyssey movement" below. |
| Cappy (`Cappy`) | R, controller RB | Super Mario Odyssey's cap throw and cap bounce; needs Odyssey movement: see "Cappy" below. |
| Point to move (`PointToMove`) | hold the left mouse button | Mario runs toward the point under the cursor while you hold it; keys still jump, spin and crouch: see "Point to move" below. |
| Hold Space to skip cutscenes and dialogue (`SkipCutscenes`) | none (hold Jump/A: Space, controller bottom button) | Holding A for 1.5 s skips the cutscene or dialogue on screen, and everything after it until you have control again: see "Skip cutscenes and dialogue" below. |
| Generic character (`GenericCharacter`) | none (a look) | The player is drawn as a plain figure with Mario's proportions and animations instead of Mario: see "Generic character" below. |

## Turning mods on

- In the game: F1, then **Mods**. Each line shows On or Off; choose it to switch.
  The choice is saved at once in `mods.txt` in the user directory. The same
  page leads to **Level Select** (native/LEVEL_SELECT.md).
- Or edit `mods.txt` (next to `controls.txt`):

  ```
  CollectStarBits=on
  ShootEnemy=off
  ```

- For one run: `PETARI_MODS=CollectStarBits,ShootEnemy` (or `none`) overrides
  the file.

The buttons are remappable in `controls.txt` like any other action:
`ModCollectStarBits=Key:G,Pad:LeftStick` and `ModShootEnemy=Key:V`
(see native/CONTROLS.md for input names). A button press while its mod is
off does nothing and is not remembered.

## Odyssey movement

Mario on foot moves with Super Mario Odyssey's physics: SMO's run speed and
acceleration, jump heights, air control, gravity and moveset. Turn it on in
F1 → Mods (`OdysseyMovement=on` in `mods.txt`, or `PETARI_MODS=OdysseyMovement`
for one run). Switching it mid-air takes effect from the next jump. Swimming,
the power-up suits (Bee, Boo, Spring, Fire, Ice, Flying) and riding things stay
Galaxy's.

"Jump during a ground pound dives" (`OdysseyJumpDive` in `mods.txt`, a line on
the Mods page) is on by default with Odyssey movement: during a ground pound in
the air (Shift or LT), pressing Jump dives, the same dive as Spin there, once per
jump (a Cappy bounce gives it back). A Jump press within 5 frames of the pound
landing is left for the ground-pound jump instead. A plain jump's Jump presses
are Galaxy's as before. Turn it off to dive with Spin only.

| Move | Keyboard | Controller | Numbers (measured in the game unless marked) |
| --- | --- | --- | --- |
| Run | WASD | left stick | 14 u/f top speed after 40 frames (SMO 14) |
| Single, double, triple jump (hold for height; the next jump within 10 frames of landing, at full speed, within 45° chains) | Space | bottom button | 258 / 346.5 / 550 high when held (SMO the same) |
| Long jump | run, Shift, then Space | run, LT, then bottom button | 144 high; landing with a direction held keeps running at the landing speed (about 23 u/f, slowing to 14 over about 75 frames) |
| Backflip | standing, Shift, then Space | standing, LT, then bottom button | 496 high |
| Sideflip | run, flick the stick back, Space | run, flick the stick back, bottom button | 496 high |
| Ground pound | Shift in the air | LT in the air | falls at up to 45 u/f (model) |
| Ground-pound jump | Space 5–30 frames after a ground pound lands | bottom button, same timing | 513.5 high |
| Dive | F or Space during a ground pound (Space: "Jump during a ground pound dives", on by default) | left button / RB or the bottom button during a ground pound | 182 up, 20 u/f forward; landing with a direction held rolls forward into a run at the landing speed (about 20 u/f); stick released: belly landing and stop |
| Roll | Shift, then F, on the ground | LT, then left button / RB | 20 u/f, slowing 0.2% a frame; ends below 17 (about 80 frames), then runs on at that speed with a direction held, or skids to a stop with the stick released |
| Roll boost | F while rolling (15 frames apart) | left button / RB while rolling | back up to 23 u/f |
| Long jump out of a roll | Space while rolling | bottom button while rolling | as the long jump |
| Wall slide | jump into a wall and hold the stick toward it | same | held 3 frames, then slides down faster (0.5 u/f² up to 35) |
| Wall jump | Space while on a wall | bottom button while on a wall | 267 high, 8.6 u/f away from the wall |

The measured numbers were measured with the movement harness (`PETARI_SMOKE=movement`)
and matches SMO's numbers exactly; details in docs/dev/ODYSSEY_MOVEMENT.md.
`native/tools/movement_baseline.py` checks them (and, with the mod off, Galaxy's
own numbers).

Known gaps in this version:

- Cappy is a separate mod (below). F (Spin) keeps Galaxy's Star Spin except
  for the dive and the roll.
- Galaxy has no animations for some SMO moves. The dive flies in Galaxy's
  long-jump leap, played quickly into its end pose (both arms swept back); the roll
  repeats the forward roll of a failed dive's landing back to back.
- No ground-pound roll (SMO: Y on a ground-pound landing). Rolls speed up
  downhill and slow uphill with SMO's constants, but how SMO scales that by
  slope is not decompiled, so it may differ a little.
- Slopes and slides keep Galaxy's behaviour; Galaxy's own wall-jump surfaces
  decide where Mario can wall-slide (walls Galaxy does not let him cling to stay
  that way).
- Spin for the dive, the roll and its boosts answers at once (Galaxy's spin
  cooldown does not apply to them, and a press up to 6 frames early still counts).
  The emulated Wii Remote shake behind F repeats at most every 250 ms (about 15
  frames), the same as the roll's boost interval, so mashing F at that rate
  occasionally skips a boost.

## Cappy

Mario throws his cap like in Super Mario Odyssey, and can bounce off it. Turn it
on in F1 → Mods (`Cappy=on` in `mods.txt`, or `PETARI_MODS=OdysseyMovement,Cappy`
for one run). It needs Odyssey movement: with that off, Cappy does nothing and
the Mods line reads "Cappy: On (needs Odyssey movement)". The cap model loads
with the stage, so turning Cappy on takes effect at the next stage load (a
level, the observatory, or Level Select). Only normal Mario throws (not the
power-up suits, not swimming).

| Move | Keyboard | Controller |
| --- | --- | --- |
| Throw (Cappy flies about 680 units ahead, hovers, and comes back) | R | RB |
| Keep Cappy out longer (up to 2 seconds) | hold R | hold RB |
| Air throw (a small hop; no new height while above the last throw) | R in the air | RB in the air |
| Dive (as Spin) | R during a ground pound | RB during a ground pound |
| Roll, roll boost (as Spin) | R while crouching, while rolling | RB, same |
| Cap bounce | jump, fall, dive or ground pound into the hovering cap | same |

The signature loop: Space (jump), R (throw), Space (the jump-press dive; or
Shift then R, as in SMO): Mario dives into the hovering cap, bounces off it, and
can dive again (Space, or Shift then R). One cap bounce per airtime; landing gives it back, a wall jump takes it
away until the next landing. A bounce from a dive is 231 units high, from a jump
or fall 300, from a ground pound 391.

With the Cappy mod on, RB throws instead of spinning (the left button still
spins; with the mod off RB spins as before). R and RB are remappable in
`controls.txt` (`ModCappyThrow=Key:R,Pad:RightShoulder`).

Known gaps in this version:

- Luigi keeps his cap on and throws a red one (Galaxy has no hair model for
  him).
- Cappy hits enemies with Mario's spin punch and collects Star Bits and coins
  he touches (coins not yet checked in game). No captures, homing throws or
  cap-return jumps.
- SMO's own Cappy parameters are not published: the flight follows the
  community's frame measurements; the hover without holding (10 frames) and
  the return speed are our own choices. A tapped throw stays bounceable for its
  short hover, and a dive while Cappy is out (R or Space) keeps him there as if
  held, for half a second, so the tapped loop works. The bounce needs Mario to
  touch the cap (it grows when fully thrown), and gives back the jump-press dive.
  Details and
  sources: docs/dev/CAPPY.md.

## Odyssey camera

A player-controlled orbit camera like Super Mario Odyssey's, wherever Galaxy uses
an ordinary follow camera. Cutscenes, boss intros, talk, launch stars, cannons,
2D sections and fixed or rail cameras stay as designed. Turn it on in F1 → Mods
→ Odyssey camera..., which also has speed (1-5) and invert options; they are
saved in `camera.txt` (`PETARI_ODYSSEY_CAMERA=1` for one run). It works with or
without the Odyssey movement mod.

| Input (camera mod on) | Action |
| --- | --- |
| Right stick | orbit; the stick no longer moves the Star Pointer (the mouse still does) |
| J / L, I / K (and Q / E, arrow left / right) | orbit left / right, pitch up / down |
| Hold Command or the middle mouse button and move the mouse | orbit; the pointer stays put |
| Two-finger trackpad or Magic Mouse scroll | orbit |
| Mouse wheel clicks, trackpad pinch, Z / X | zoom in / out |
| C | recentre behind Mario |

All of these are remappable in `controls.txt` (`CameraOrbitHold`,
`CameraOrbitLeft`, `CameraOrbitRight`, `CameraPitchUp`, `CameraPitchDown`,
`CameraZoomIn`, `CameraZoomOut`). `ScrollMode=orbit` or `zoom` in `camera.txt`
makes every scroll orbit or zoom. Design and value sources: docs/dev/ODYSSEY_CAMERA.md.

### Photo mode

Freeze the game and fly a free camera to frame a picture. Turn it on in F1 → Mods
→ Odyssey camera... → **Photo mode** (`PhotoMode=on` in `camera.txt`,
`PETARI_PHOTO_MODE=1` for one run). It does not need the orbit camera. Then press
**P** (gamepad: click the right stick) during normal play, wherever the pause menu
could open. The game stops as it does in the pause menu, the HUD is hidden, and
the camera starts where the game's camera was.

| Input (photo mode) | Action |
| --- | --- |
| W A S D, left stick | fly forward, left, back, right |
| Space / Shift | fly up / down |
| Hold any mouse button and move the mouse, trackpad scroll, right stick, J / L / I / K, Q / E, arrows | look around |
| Mouse wheel, pinch, Z / X | narrower / wider view (10 to 120 degrees) |
| Hold Tab / Alt | 5x faster / 5x slower |
| O or Return | save a screenshot |
| P or Escape | back to the game, exactly where it was |

Screenshots are the game's full rendered frame without the HUD, saved as PNG in
`~/Pictures/Petari/` (`PETARI_PHOTO_DIR=<folder>` puts them elsewhere). While
photo mode is on, the game receives no input. Keys held when you entered are let
go. Mario, enemies, timers and the camera are exactly as they were when you
leave. Objects the game had hidden because they were out of its camera's view
stay hidden while you fly. `PhotoMode` and `PhotoShot` are remappable in
`controls.txt`. The other photo controls follow the bindings they share with play
(the stick keys, A, NunchukZ, the camera keys, TiltHold, Walk, Start, Plus).

## Point to move

Turn on "Point to move (hold left click)" (`PointToMove=on`). Then, in normal
play, **hold the left mouse button**: Mario runs toward the point under the
cursor, and stops when you let go, as if the stick were released. He always
runs, at full speed, wherever you point; only a small circle around him (3% of
the screen height) does nothing, so he does not jitter with the cursor on him.
(Hold Left Alt to walk.) The direction follows the cursor and Mario every
frame. Your keys keep their jobs while you steer: Space jumps (in the direction
you are running), F spins, Shift crouches or ground-pounds (a long jump is
Shift, then Space while running), Left Alt walks.

- **It is the stick.** The pointer's direction from Mario on the screen becomes
  the Nunchuk stick, so the game moves him exactly as with the stick: on round
  planets, upside down, in water, and as Bee, Boo, Spring, Ice and Fire Mario.
  WASD (or a controller stick), when held, wins over the pointer. On the Star
  Ball and the Ray (Manta), which steer by tilting the remote, the pointer tilts
  it the way the stick keys do there (native/CONTROLS.md): the Star Ball rolls
  toward the pointer, the Ray turns toward its side. Always at full tilt, as a
  held key; let go to stop tilting.
- **Shooting Star Bits.** While the mod is on, a left press in normal play steers
  and sends no B, from press to release. Shoot with the **B key** instead (the
  Wii B: it shoots a Star Bit at the pointer, and holding it strokes the pointer
  objects that need "hold B", such as turning platforms). The B key works with
  the mod off too. The Shoot Enemy (V) and Collect Star Bits (G) mods work as
  before.
- **Only in normal play.** Outside it (menus, mission select, the pause menu,
  cutscenes, conversations, the Home menu and the editor) left click is B as
  always. A hold that started in normal play stays a steering hold until you let
  go: if a cutscene starts meanwhile, Mario just stops, and no B reaches it.
- Right click (Pull Stars, pointer grabs) is unchanged.

Test: `native_input` checks the stick's dead zone and that it is otherwise full, that a claimed press
sends no B, WASD wins, release stops, and that left click stays B outside
normal play. `PETARI_POINT_PROBE=1` (native/app/point_probe.cpp, with
`PETARI_MODS=PointToMove`) drives the real mouse path next to any smoke script
and logs `[point-probe]` checks: Mario's movement on screen follows the pointer
up, right and down-left, he runs with the pointer just outside the dead zone, he
stops on release, Space jumps mid-run, and in the
pause menu left click is B.

## Generic character

Turn on "Generic character (not Mario)" (`GenericCharacter=on`). The player is
then drawn as a plain figure: a round head, a bucket hat, a slate suit, mittens
and dark shoes. It is built at runtime from ellipsoids and capsules in flat
colours, lit by the game's player lights. No Mario mesh or texture is drawn for
it. The figure sits on Mario's own skeleton, so it has his proportions and
moves with every one of his animations (run, jumps, spin, long jump, swimming,
and so on). Only the drawing changes: movement, collision, hit boxes and sounds
are the game's.

- **Power-ups** recolour the figure. Fire: a white suit and an orange hat.
  Flying (Red Star): a dark suit and a gold hat. Rainbow: the suit cycles
  through colours. Ice: pale cyan all over. Bee: a yellow suit and a dark hat
  (the game's bee wings stay). Spring: metal grey, with no coil drawn. Boo: a
  plain white ghost with the same hat (the Boo Mario model is hidden).
- **Cappy** (with the Cappy mod): the thrown cap is the figure's hat, spinning
  and growing as Cappy does, and the head is bare while it is out.
- **Not drawn with the mod on:** Mario's reflection in mirror floors, the
  rainbow afterimages, and the light-blue see-through-walls silhouette (the
  game builds it from Mario's own models), so the figure is hidden behind walls
  like any other object.
- **Still Mario:** his voice, the HUD and life-meter icons, the ground shadow (a
  soft blob from Mario's shadow model), characters in cutscenes that are
  separate models rather than the player, the Cosmic Mario racer, and dialogue
  that names him. First-person view shows the mittens.

Test: `PETARI_FIGURE_PROBE=1` (native/app/figure_probe.cpp) plays a fixed key
script once normal play is steady (idle, run, jump, spin, long jump, Cappy
throw, dive) and saves `figure-<pose>.png` screenshots to
`PETARI_FIGURE_PROBE_SHOTS`. Run it with the mod on and off to compare.

## Skip cutscenes and dialogue

Turn on "Hold Space to skip cutscenes and dialogue" (`SkipCutscenes=on`). Then,
while a cutscene, a movie or a conversation is on screen, hold Space (anything
bound to A: Space, the controller's bottom button, the right mouse button,
Return) for 1.5 seconds (90 game frames). Taps still work as before (A shows
the text and turns the page); only a long hold skips.

Once a hold skips, the skip goes on until you have control of Mario again:
every remaining page of the conversation, any cutscene that follows it, and any
conversation inside a cutscene. It stops early:

- **At a choice.** A yes/no question (a talk's choice, the save prompt) is never
  answered for you. The game has no default answer for these (you point at one),
  so the skip ends there. Answer it, then hold again to skip what follows.
- When a menu opens that waits for you: the galaxy map (Rosalina's "Would you
  like to see the map?") or the pause menu.
- At a stage change (a movie that leads to a new galaxy, a star that takes you
  back to the observatory). Hold again on the new stage.
- After 60 seconds without reaching gameplay (logged; a safety net).

The hold that skipped never reaches the game: the A button stays released for
the game until you let go of Space, so it doesn't become a jump, a talk or a
menu choice afterwards. Let go and press again to jump.

How each kind ends. It always goes through the game's own end path, so every
flag, camera reset, music change and scene change still happens:

| What is on screen | What the skip does |
| --- | --- |
| A movie (the prologue's two, the final battle, epilogue, ending) | The game's own movie skip (on the Wii only the final battle had one, after a visit): stop the movie, close the wipe, then its normal end (music, the next stage). |
| The camera fly-around entering a galaxy | The game's own skip of it, also on a first visit. |
| The launch star flight into a galaxy | The game's own skip of it, also on a first visit. |
| A conversation page | The page is shown whole and turned, as an A press would (no A is sent). The last page before a choice goes to the choice. |
| The prologue's storybook and Peach's letter, Peach's and Luigi's letters in the observatory, Rosalina's storybook pages | Turned and closed as an A press would. The storybook's chapter list waits for you (it ends the skip). |
| After a star: the result screen (star count, best score, Star Bits), new-galaxy notices and map, first-time power-up notices | Continued as an A press would; the save prompt that follows is a choice, so the skip stops there. |
| Any other cutscene (story scenes, boss introductions, a star appearing, the star-get celebration, Rosalina and the Lumas, ...) | Fast-forward: the game runs its own update several times per drawn frame (up to 15, about 10 ms of extra work a frame) until the cutscene ends. Nothing is jumped over, so each step of the cutscene runs in order, with its sound effects muted. |

The log shows `[skip] ...` lines: when a skip latches and ends (and why), each
movie, camera, launch and page it ends, and how many extra updates the
fast-forward ran.

Tests: `native_input` checks the hold count, the latch, the end conditions and
that the hold never reaches the game (through the game's own `WPadButton`).
`PETARI_SKIP_PROBE=1` (with `PETARI_MODS=SkipCutscenes`) is the in-game proof:
next to any smoke script, it holds Space through each cutscene and dialogue and
logs `[skip-probe]` lines (what was on screen, the latch, the end, whether the
game saw A, Mario afterwards). `PETARI_SKIP_PROBE_SHOTS=<dir>` adds
screenshots. See native/app/skip_probe.cpp.

## How they follow the game's rules

Both mods act only where the game itself would let the pointer collect or
shoot Star Bits: not during cutscenes, not while the Star Pointer is disabled
(for example in the file select's two-player transparency mode), and not while
the game is paused (they run in the Star Bit director's per-frame update,
which stops with the scene).

- **Collect** uses the game's own collection path: for four frames after the
  press, `StarPiece::tryGotJudge` treats every eligible Star Bit as pointed at,
  so `goToPlayer` sends it to Mario with the normal sparkle, sound and counter.
  Eligible means what the pointer could collect (within the game's collection
  distance, past its short delay after appearing) and on screen: not clipped,
  not hidden, inside the camera frustum.
- **Shoot** uses `StarPieceShooter::shoot`'s path: `StarPiece::throwToTarget` from
  the usual shot origin, the usual shot sound, one Star Bit spent. The hit, stun
  and damage are the enemy's normal response to a Star Bit. The target is the
  enemy sensor nearest to Mario that is valid, inside the camera frustum, whose
  actor is not clipped or hidden, and that the camera can see (no map polygon
  between). No target means no shot. With no Star Bits it plays the game's
  "no Star Bits" sound, as a normal shot would.

Diagnostics go to the log as `[mods] ...` lines (what was collected, what was
shot at, what it hit, and "not allowed now" / "no enemy on screen").

## Testing

- `native_input` covers the bindings, press counting, dropped presses while a
  mod is off, and `mods.txt`/`PETARI_MODS` parsing; `native_home_menu` covers the
  Mods page.
- `PETARI_MODS_AUTOPRESS=<frames>` (test driver in `app_main.cpp`) presses the
  collect button every `<frames>` gameplay frames and the shoot button halfway
  between, so background smoke runs can exercise the mods; with the mods off the
  same presses must change nothing.

# Mod folder: replacing or adding disc files

The mod folder lets a mod replace any file of the game disc, or add new ones
(custom stages need new archives), without touching your game copy. Mods are a
folder of files that mirror the disc's `files/` tree. They are **off by default**:
with no mods, or none enabled, the game reads the disc exactly as before.

## Layout

```
<user folder>/mods/                 (also: every directory in PETARI_MOD_DIRS, ":"-separated, for development)
  MyMod/
    mod.txt                         optional
    files/
      StageData/EggStarGalaxy.arc   replaces the disc's /StageData/EggStarGalaxy.arc
      StageData/MyStage/MyStage.arc a new file (the folder MyStage is created)
      LayoutData/...  ObjectData/...  AudioRes/...  (anything below the disc's files/)
```

- The user folder is the one given by `--user` (the folder that holds `NAND/`,
  `controls.txt` and `mods.txt`). If a mod name exists in several of the searched
  folders, the first one found wins (the user folder before `PETARI_MOD_DIRS`).
- `mod.txt` has `name=` (shown in the menu), `description=` and `priority=` (a
  whole number, default 0). Unknown lines are ignored.
- Paths match the way the game matches them: ASCII case-insensitively, `/` or `\`
  separators, leading slashes ignored. `..` is refused. The case of the file's
  path in your mod is kept when it adds a new file.
- Hidden macOS files (`.DS_Store`, `._*`) are ignored.
- A file may be any size: a replacement larger than the original, and added files,
  are placed after the disc's last file in the virtual disc. Files read by the
  game, by streamed audio and by movies all go through the same virtual disc.

## Turning mods on

- In the game: F1, **Mods**, **Mod folder...**. Each mod shows On or Off, three to a
  page. Changes are saved at once in `mods.txt` as `Folder.<ModName>=on|off` and
  apply **the next time the game starts** (the disc is read once at start-up). The
  gameplay mods' lines (`CollectStarBits=on`...) share the file and are not touched.
- Or edit `mods.txt`:

  ```
  Folder.MyMod=on
  ```

- For one run: `PETARI_MOD_FOLDERS=MyMod,OtherMod` (or `none`) overrides the file.

## Precedence and the log

Enabled mods are ordered by `priority` (higher first), then by name. For a file
supplied by several mods the first one in that order wins. At start-up the log
says what happened:

```
[mods] mod MyMod (priority 5, rank 1 of 2): 14 files
[mods] conflict StageData/EggStarGalaxy.arc: MyMod wins over OtherMod
[mods] disc overlay applied: 3 files replaced, 11 added
[mods] game opened /StageData/EggStarGalaxy.arc from mod MyMod (91616 bytes)   <- first open of each mod file
```

A mod file that cannot be used (for example a path where the disc has a directory
in the way) is skipped with a `[mods] skipped <path>: <reason>` line; the rest of the
mod still applies.

## Compressed and uncompressed archives

The game decides by the file's header, as on the console. Stage archives
(`.arc`) can be Yaz0-compressed (header `Yaz0`) or plain (`RARC`); both load.
Whitehole writes the compressed form; either is fine in `files/`. Nothing in the
mod folder is converted.

## Broken files are refused, not loaded

Before a mod file can replace a disc file, its header is checked (a few dozen bytes, at start-up):
archives (`.arc`, `.szs`) need a Yaz0 header with a sane size and, for `.arc`, a RARC or U8 archive after
it, or a RARC or U8 header whose sizes fit the file; `.bmg` needs `MESGbmg1`; `.brstm` needs `RSTM`. Other
files are not checked. A file that fails is skipped and the disc file (or another mod's valid file) is
used, with a log line:

```
[mods] MyMod: StageData/EggStarGalaxy.arc is not a valid archive; using the disc file
```

This only catches obvious damage (a truncated download, the wrong file, a corrupted header); an archive
with a valid header and bad contents can still fail when the game uses it. The check was run against all
2,233 archive, message and stream files of a real disc with no false rejections.

## Make your first mod

1. Pick a file to change. The disc's files are in `build/game-data/RMGE01/files/`
   (your own extracted copy). For example `StageData/EggStarGalaxy.arc` (a stage).
2. Make the folder: `<user folder>/mods/MyFirstMod/files/StageData/`.
3. Put your edited archive there with the same name:
   `<user folder>/mods/MyFirstMod/files/StageData/EggStarGalaxy.arc`. With Whitehole:
   open the galaxy, change something, save, and copy the archive Whitehole wrote
   into the folder above. Your original files are never modified.
4. Optional: `<user folder>/mods/MyFirstMod/mod.txt` containing
   `name=My first mod` and `description=Moves the start point`.
5. Start the game, press F1, **Mods**, **Mod folder...**, switch **My first mod** On,
   quit and start the game again (or start it with `PETARI_MOD_FOLDERS=MyFirstMod`).
6. Check the log for `[mods] disc overlay applied: 1 files replaced, 0 added` and
   `[mods] game opened /StageData/EggStarGalaxy.arc from mod MyFirstMod`.

To undo it, switch the mod off (or delete its folder). Nothing else changed.

## A verification example with no Nintendo data

`native/tools/mod_smoke.py` builds a test mod at run time from your own copy of
the disc and checks it in the real game, without shipping any game data:

```sh
python3 native/tools/mod_smoke.py --app build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari --output build/mod-smoke
```

It decompresses `EggStarGalaxy.arc`, appends 4096 zero bytes and raises the header's size field to match (a valid,
larger archive), and installs the result as an uncompressed archive and as a Yaz0 stream. It then
launches the game four times on that stage: mod present but not enabled (the archive must mount at
its disc size), enabled uncompressed, enabled Yaz0 (the archive must mount 4096 bytes larger, the log must
show the overlay and the first open of the mod's file, and the stage must still load and pass), and a fourth with a corrupt archive (refused with the log line above;
the disc archive loads and the stage passes). The size
the game mounted comes from its own `[heap-arc] /StageData/EggStarGalaxy.arc -> file cache, N bytes` line.

(Do not use Mario's start point as the probe: the stage fixture's synthetic entry does not take it from
the archive's start rows. Corrupting the archive's `RARC` magic crashes the game when it loads, which is
the other way to show the game reads the mod's bytes.)

## Tests

- `native_platform_dvd` (`testOverlay`): replacement of a file the same size, smaller
  and larger, other-case paths, new files and directories, conflicts (a file where a
  directory is, and the reverse), byte reads, absolute disc-offset reads, and the
  unmodified mount.
- `native_mod_folder`: header validation (good and corrupt RARC, U8 and Yaz0 archives, BMG, BRSTM; with
  `PETARI_DISC_FILES=<disc>/files` it also checks every real disc archive), path normalization (case, slashes, leading slashes, `..`),
  discovery, `mod.txt`, the states in `mods.txt` (other lines kept), default off,
  precedence and conflict logging, a missing `files/` directory, `PETARI_MOD_FOLDERS`
  and `PETARI_MOD_DIRS`.
- `native_input`: `Folder.` lines are accepted by the gameplay mods parser and kept when
  it saves. `native_home_menu` (`testModFolderPage`): the page, paging, toggles.
- `native/tools/mod_smoke.py`: the live check above.

## Make a level edit

A level edit is a stage archive with changed placements, put in a mod folder. The
game loads it in place of the disc's archive; nothing else is needed.

**With Whitehole** (the usual Galaxy level editor): point it at your extracted disc,
open the galaxy, move or add objects, and save. Whitehole writes the stage
archive (for example `StageData/EggStarGalaxy.arc`, Yaz0-compressed). Copy that
file into `<user folder>/mods/<YourMod>/files/StageData/`, under the same name, and
switch the mod on (see "Make your first mod"). Work on a copy of the disc files:
Whitehole saves in place.

**With `native/tools/stage_edit.py`** (small edits from the command line, and
the tests): it reads an archive (Yaz0 or plain), changes rows in its placement
tables, and writes a new archive. The disc file is only read.

1. See what is in a stage:

   ```sh
   A=build/game-data/RMGE01/files/StageData/EggStarGalaxy.arc
   python3 native/tools/stage_edit.py tables $A
   python3 native/tools/stage_edit.py show $A jmp/placement/common/objinfo
   python3 native/tools/stage_edit.py show $A jmp/start/layera/startinfo
   ```

   Each row is printed as `#index  name  l_id  (x, y, z)`. Tables are per layer
   (`common`, `layera`...): a scenario uses `common` plus its own layers.
   Positions are in the stage's own space. Objects in a zone archive are moved
   by that zone's placement in the galaxy.

2. Make the edit: a ring of coins around Good Egg's first start point, and a
   Launch Star moved up:

   ```sh
   M="<user folder>/mods/CoinRing/files/StageData"
   python3 native/tools/stage_edit.py edit $A "$M/EggStarGalaxy.arc" \
     --new jmp/placement/common/objinfo Coin -2914.6 -12961.3 -15332.0 \
     --new jmp/placement/common/objinfo Coin -3614.6 -12961.3 -15332.0 \
     --move jmp/placement/common/objinfo SuperSpinDriver@0 0 500 0
   ```

   - `--move TABLE ROW DX DY DZ` moves a row.
   - `--set TABLE ROW FIELD=VALUE` sets any field (for example `Obj_arg0=3`, `name=Kuribo`).
   - `--add TABLE ROW X Y Z` copies a row to a new place.
   - `--new TABLE NAME X Y Z` adds a new object with default settings.

   A ROW is `NAME@N` (the Nth row with that name, from 0) or `#N`. New rows get
   an unused `l_id`. The output is Yaz0-compressed unless you pass `--plain`.
   Everything you did not change stays byte for byte as on the disc.

3. Switch the mod on (`Folder.CoinRing=on` in `mods.txt`, or the Mods page) and start
   the game. To check what the game placed, start it with
   `PETARI_PLACEMENT_TRACE=Coin,SuperSpinDriver` (or `all`). The log then lists each
   placed object of those names with its world position:

   ```
   [mods] game opened /StageData/EggStarGalaxy.arc from mod CoinRing (25854 bytes)
   [placement] Coin zone 0 l_id 125 at (-2914.6, -12961.3, -15332.0)
   [placement] SuperSpinDriver zone 0 l_id 4 at (-12216.7, -14924.5, -3022.8)
   ```

A new object must be one the game knows (the names in
`src/Game/NameObj/NameObjFactory.cpp`). Its model and sounds must be loadable in
that stage; common objects such as `Coin`, `StarPiece` and `Kuribo` are. Some
objects need their own settings (`Obj_arg`s, switches, a path) to do anything;
copy a working one with `--add` and change it.

### The live check

`native/tools/level_edit_smoke.py` makes the CoinRing mod above at run time from
your copy of the disc, so no Nintendo data is shipped. It launches Good Egg
mission 1 twice:
- with the mod present but off: no new coins, and the Launch Star where the disc
  has it;
- with it on: the overlay is applied, the game opens the mod's archive, and the
  log shows the 8 coins and the moved Launch Star where the edit put them.

Screens from the idle phase are kept in `<output>/<run>-xfb/`.

```sh
python3 native/tools/level_edit_smoke.py --app build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari \
  --output build/level-edit-smoke
```

`native_stage_edit` (ctest) covers the tool without Nintendo data: Yaz0 round trips,
an unedited rebuild identical to its source, moves, field sets, added and new rows,
unique `l_id`s, and errors. With an extracted disc in `build/game-data`, every stage
archive must also rebuild byte for byte when unedited.
