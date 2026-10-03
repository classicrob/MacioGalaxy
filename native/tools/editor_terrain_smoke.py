#!/usr/bin/env python3
"""Live check of terrain collision built in memory (spike S6, docs/dev/level-editor/TECH.md 6.3).

Stage smokes on EggStarGalaxy mission 1 with PETARI_EDITOR=1 PETARI_EDITOR_TERRAIN_TEST=1, each in its
own fixture user folder. src/Game/Editor/EditorTerrainTest.cpp builds a box and a sphere brush with the
editor's KCL writer on the first gameplay frame, creates collision parts from the in-memory KCLs and
fits cube and point gravity to them; the smoke's test warp (PETARI_STAGE_WARP) then puts Mario on one,
and the smoke's own walks, jump and spin run there. Each run must log "[editor-terrain] PASS" and the
smoke must pass.

  box     warp to (-20000, 400, 30000): above the box's top face (y = 150)
  sphere  warp to (-20000, 1250, 40000): above the sphere's north pole (y = 1000)

The -patch runs (box-patch, sphere-patch) are the same scene saved as a patch mod (petari_terrain_bake
--patch: edits/EggStarGalaxy.json with the two brush rows, no game data); the mod loader bakes them at
start-up (Terrain::bakePatchAssets) and the game loads the result like the -archive runs.
The -archive runs (box-archive, sphere-archive) build nothing live: petari_terrain_bake writes the same
box and sphere as a Full-archive mod into the fixture user (ObjectData planet archives with a .bmd model,
.kcl and .pa; PlanetMapDataTable rows; placement and gravity rows in EggStarGalaxy.arc), enabled with
PETARI_MOD_FOLDERS, and the game loads them through its own placement (PETARI_EDITOR_TERRAIN_TEST=archive).

usage: editor_terrain_smoke.py --app Petari.app/Contents/MacOS/Petari --output build/editor-terrain-smoke
       [--runs box,sphere,box-archive,sphere-archive,box-patch,sphere-patch] [--bake build/macos-gx/native/editor/petari_terrain_bake]
       [--owner editor-terrain]
Screenshots (<output>/{live,archive}-{box,sphere}-{90,420}.png) after 90 and 420 frames on a part.
One app at a time through build/locked-app.sh.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stage_sweep as sweep  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
STAGE, SCENARIO = "EggStarGalaxy", 1
WARPS = {"box": "-20000,400,30000", "sphere": "-20000,1250,40000"}


def run(args, name, extra):
    user = args.output / f"{name}-user"
    shutil.rmtree(user, ignore_errors=True)
    sweep.load_fixture_module().create(args.source, user, "stage")
    if name.endswith("-archive"):
        files = user / "mods" / "TerrainBake" / "files"
        subprocess.run([str(args.bake), "--disc", str(args.disc / "files"), "--out", str(files)], check=True)
        extra = dict(extra, PETARI_MOD_FOLDERS="TerrainBake")
    if name.endswith("-patch"):
        files = user / "mods" / "TerrainPatch" / "files"
        subprocess.run([str(args.bake), "--disc", str(args.disc / "files"), "--out", str(files), "--patch", "1"], check=True)
        extra = dict(extra, PETARI_MOD_FOLDERS="TerrainPatch")
    log = args.output / f"{name}.log"
    command, env_extra = sweep.app_command(args, user, STAGE, SCENARIO)
    env = dict(os.environ, **env_extra)
    for key in ("PETARI_EDITOR", "PETARI_EDITOR_HOOKS_TEST", "PETARI_EDITOR_TERRAIN_TEST", "PETARI_STAGE_WARP", "PETARI_MOD_FOLDERS",
                "PETARI_EDITOR_TERRAIN_SHOTS"):
        env.pop(key, None)
    env.update(PETARI_LOCK_CLASS="functional", **extra)
    full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
    with log.open("wb") as stream:
        status = subprocess.run(full, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    return {
        "name": name, "status": status,
        "result": (re.findall(r"PETARI SMOKE RESULT: (\w+)", text) or [None])[-1],
        "terrain": re.findall(r"\[editor-terrain\] (.*)", text),
        "checks": re.findall(r"stage check (\S+): (\w+)", text),
        "mods": re.findall(r"\[mods\] (game opened .*|disc overlay .*|skipped .*|patch mods.*|terrain: .*|mod .*: not applied.*)", text),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01")
    parser.add_argument("--source", type=Path, default=ROOT / "build/observatory-user-2")
    parser.add_argument("--lock", type=Path, default=ROOT / "build/locked-app.sh")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--owner", default="editor-terrain")
    parser.add_argument("--runs", default="box,sphere")
    parser.add_argument("--bake", type=Path, default=ROOT / "build/macos-gx/native/editor/petari_terrain_bake")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--idle-frames", type=int, default=400)
    args = parser.parse_args()
    args.pipeline_policy = "default"
    args.output.mkdir(parents=True, exist_ok=True)

    failures = []
    for name in args.runs.split(","):
        shape = name.split("-")[0]
        mode = "archive" if name.endswith("-archive") or name.endswith("-patch") else "1"
        r = run(args, name, {"PETARI_EDITOR": "1", "PETARI_EDITOR_TERRAIN_TEST": mode, "PETARI_STAGE_WARP": WARPS[shape],
                             "PETARI_EDITOR_TERRAIN_SHOTS": str(args.output.resolve())})
        print(f"{name}: exit {r['status']}, smoke {r['result']}; checks " + ", ".join(f"{c}={v}" for c, v in r["checks"]))
        for line in r["mods"]:
            print(f"  [mods] {line}")
        for line in r["terrain"]:
            print(f"  [editor-terrain] {line}")
        if not any(line.startswith("PASS") for line in r["terrain"]):
            failures.append(f"{name}: terrain check did not pass")
        if r["result"] != "PASS":
            failures.append(f"{name}: stage smoke {r['result']}")
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("PASS: Mario walked on generated collision (" + args.runs + "), with fitted gravity")
    return 0


if __name__ == "__main__":
    sys.exit(main())
