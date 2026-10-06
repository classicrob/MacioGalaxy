# demo_video: player-input demo videos

One tool for the "play it with the keyboard, record it, overlay the keys" videos (level demos, mod demos).

```sh
python3 native/tools/demo_video/demo_video.py make --slug my-level --title "My Level" \
    --app <build>/native/app/Petari.app/Contents/MacOS/Petari \
    --save <private COPY of a save dir> --stage <Galaxy> --script keys.txt \
    [--highlight 300-700] [--slow] [--layout band|overlay] [--crop x,y,w,h] [--owner <your lane>]
```

Writes `~/Petari-playtest/levels/<slug>/`: `<slug>.mp4` (H.264 1280x720 60 fps, real time), `-highlight.mp4` /
`-slow.mp4` (0.5x, optional), `-inputs.csv` (the raw per-frame log), `-moves.log`, `-sheet.png` (12-frame contact sheet:
look at it), `-keys.txt`, `-run.json`. The raw frame dump (about 11 MB a frame) and the save copy are deleted; `--keep-run`
keeps them. The tool refuses to start when the dump would not fit in free disk (keeping 10 GiB).

## Your inputs
- **Build**: a Petari with `PETARI_INPUT_SCRIPT` and the dump mark (`EfbDump::mark("input-script")` in
  `native/app/input_script_probe.cpp`). Without the mark nothing is dumped and the tool says so.
- **Save**: a copy of a save dir whose `mods.txt` has what you show on (OdysseyMovement, Cappy, GenericCharacter, ...),
  your level's patch folder, and `camera.txt` with `OdysseyCamera=on` if you drive the camera. It is cloned per run.
- **Key script**: lines of `<frames> [key ...]` (`#` comments), or build one with `keys.py` (`Plan`). Keys are held for that
  many frames; presses and releases are sent on changes. Names are controls.txt names: W A S D, Space (jump; dive in a
  ground pound), LeftShift (crouch / ground pound; long jump = run + Shift + Space), R (Cappy), F (spin), J / L orbit,
  I / K pitch, Z / X zoom. The script starts after 60 steady frames of play, with the character at the start.

W is "away from the camera": if you orbit while moving, re-aim by hand as a player would (W, W+A, A, A+S, S over a
180-degree L sweep).

## Overlay layouts
`band` (default): the scene is scaled into the area above the overlay, nothing is drawn over it, so the character is never
hidden. `overlay`: the translucent strip + timeline over the bottom of the scene (the layout of the first Cappy Planet
videos). Either way: gold key caps for moves, blue for camera keys, a timeline (one row per input in use, 150 frames
back, 60 ahead dimmed), and a "MOVING + CAMERA AT THE SAME TIME" badge.

## Sync
Script frame f is drawn on dump frame f + latency (`--latency`, default 1 = the shape-garden / le-main build, measured by
Shape Garden, Orbit Run and `calibrate`). It is a constant of a build, not of a run: calibrate a new build once with

    python3 native/tools/demo_video/demo_video.py calibrate --app <binary> --save <save copy, OdysseyCamera=on> --stage <Galaxy>

(an idle character, one camera key: the camera onset is crisp in the log and in the picture; prints `{"latency": N}`).
It is deliberately not measured from a real demo: ramping, overlapping motion cannot pin a 1-2 frame offset.

## Warm shader caches
After a clean run the warm `pipeline_cache.db` / `dawn_cache.db` are backed up (SQLite backup, consistent) back into the
`--save` copy (`--seed-caches DIR` to put them elsewhere, `none` to disable), so the next run starts warm instead of
recooking shaders. `--save` must therefore be a private copy.

## Framing
`--crop x,y,w,h` (in dump pixels; dumps are the window size, e.g. 2048x1152 or 2560x1440) crops the scene before it is
drawn; the `band` layout keeps the cropped aspect, so a wider crop uses more of the width and shrinks less.

## Pieces
`record` (run + dump), `render` (overlay + encode + sheet from a run dir), `sync`, `selftest` (synthetic run, no game;
also `ctest -R native_demo_video`, label `tools`). Rehearse a script first with `--no-dump` (fast; check `-moves.log`
and `-inputs.csv`).

`--hide-hud` sets `PETARI_HIDE_HUD=1` for the run: the game's counters, lives, meters, pointer cursor and stage banners are
hidden for a clean "generic" video (a build with the HideHud mod, wt/generic-world; menus and dialogue still show).

Move log: `-moves.log` keeps the game's `[cappy]` / `[odyssey]` move lines. Add `--env PETARI_ODYSSEY_TRACE=1` to also
keep the first frame of each Odyssey air move (`[odyssey] frame 1 kind N ...`; kind 6 = long jump).

Never signal Petari processes by name or path: the tool only signals the process group it started.
