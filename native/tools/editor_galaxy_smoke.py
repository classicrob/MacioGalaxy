#!/usr/bin/env python3
"""Live check of a new galaxy through the mod galaxy registry (spike S8, docs/dev/level-editor/TECH.md 5.7).

petari_terrain_bake --galaxy ModTestGalaxy writes a folder mod into a fixture user: the galaxy's stage and
Scenario archives (a box planet with cube gravity, a start, Power Star 1 where Mario lands), the planet's
archive and PlanetMapDataTable, and galaxy.json (names, mission name, borrowed music). ModTestGalaxy is in
no compiled-in table (GalaxyID.bcsv, event flags, Message.arc, BGM).

  1. collect  stage smoke entering ModTestGalaxy (PETARI_STAGE_FOLLOW=1: leaving the stage is followed, A
              tapped through the result screens); src/Game/Editor/EditorGalaxyTest.cpp logs the registry's
              names, waits for the star to be owned, then saves with the game's own save sequence
  2. verify   a new launch on the same user folder: entering ModTestGalaxy, the star must still be owned

Each run must log "[editor-galaxy] PASS" and the smoke must pass. Screenshots are not taken.

usage: editor_galaxy_smoke.py --app Petari.app/Contents/MacOS/Petari --output build/editor-galaxy-smoke
       [--bake build/macos-gx/native/editor/petari_terrain_bake] [--owner editor-terrain]
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
GALAXY, MOD = "ModTestGalaxy", "GalaxyTest"


def run(args, name, user, mode):
    log = args.output / f"{name}.log"
    command, env_extra = sweep.app_command(args, user, GALAXY, 1)
    env = dict(os.environ, **env_extra)
    for key in ("PETARI_EDITOR", "PETARI_EDITOR_HOOKS_TEST", "PETARI_EDITOR_TERRAIN_TEST", "PETARI_STAGE_WARP", "PETARI_MOD_FOLDERS"):
        env.pop(key, None)
    env.update(PETARI_LOCK_CLASS="functional", PETARI_MOD_FOLDERS=MOD, PETARI_STAGE_FOLLOW="1", PETARI_EDITOR_GALAXY_TEST=mode)
    full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
    with log.open("wb") as stream:
        status = subprocess.run(full, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    return {
        "name": name, "status": status,
        "result": (re.findall(r"PETARI SMOKE RESULT: (\w+)", text) or [None])[-1],
        "galaxy": re.findall(r"\[editor-galaxy\] (.*)", text),
        "mods": re.findall(r"\[mods\] (galaxy .*|game opened .*StageData.*)", text),
        "reason": (re.findall(r"PETARI SMOKE (?:FAIL|REASON)[^\n]*", text) or [""])[-1],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01")
    parser.add_argument("--source", type=Path, default=ROOT / "build/observatory-user-2")
    parser.add_argument("--lock", type=Path, default=ROOT / "build/locked-app.sh")
    parser.add_argument("--bake", type=Path, default=ROOT / "build/macos-gx/native/editor/petari_terrain_bake")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--owner", default="editor-terrain")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--frames", type=int, default=9000)
    parser.add_argument("--idle-frames", type=int, default=400)
    args = parser.parse_args()
    args.pipeline_policy = "default"
    args.output.mkdir(parents=True, exist_ok=True)

    user = args.output / "user"
    shutil.rmtree(user, ignore_errors=True)
    sweep.load_fixture_module().create(args.source, user, "stage")
    subprocess.run([str(args.bake), "--disc", str(args.disc / "files"), "--out", str(user / "mods" / MOD / "files"),
                    "--galaxy", GALAXY], check=True)

    failures = []
    for name, mode in (("1-collect", "collect"), ("2-verify", "verify")):
        r = run(args, name, user, mode)
        print(f"{name}: exit {r['status']}, smoke {r['result']} {r['reason']}")
        for line in r["mods"]:
            print(f"  [mods] {line}")
        for line in r["galaxy"]:
            print(f"  [editor-galaxy] {line}")
        if not any(line.startswith("PASS") for line in r["galaxy"]):
            failures.append(f"{name}: the galaxy check did not pass")
        if r["result"] != "PASS":
            failures.append(f"{name}: stage smoke {r['result']}")
        if failures:
            break
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("PASS: entered a galaxy outside GalaxyID.bcsv, collected its star, saved, and found it after a restart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
