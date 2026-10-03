#!/usr/bin/env python3
"""Live check of New Galaxy in the editor (docs/dev/level-editor/new-galaxy-flow.md, M4).

One stage smoke on EggStarGalaxy mission 1 with PETARI_EDITOR=1 PETARI_EDITOR_NEWGALAXY_TEST=create
(native/editor/runtime/new_galaxy_test.cpp). Once the stage checks are done it asks for a new galaxy
("Smoke Hop": FlowerPlanet with its gravity, two missions, Flower Galaxy's music). The editor writes
it as a mod in the fixture user and restarts Petari into it: the same process (execv), so this run
sees both halves. The restarted game enters SmokeHopGalaxy (the stage fixture's entry, set by the
restart), runs the stage checks there and must log "[editor-newgalaxy] PASS". The smoke must pass,
and <user>/mods/SmokeHopGalaxy must hold galaxy.json and the two archives.

usage: editor_new_galaxy_smoke.py --app Petari.app/Contents/MacOS/Petari --output build/editor-new-galaxy-smoke [--owner editor-terrain]

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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01")
    parser.add_argument("--source", type=Path, default=ROOT / "build/observatory-user-2")
    parser.add_argument("--lock", type=Path, default=ROOT / "build/locked-app.sh")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--owner", default="editor-terrain")
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--idle-frames", type=int, default=600)
    args = parser.parse_args()
    args.pipeline_policy = "default"
    args.output.mkdir(parents=True, exist_ok=True)

    user = args.output / "user"
    shutil.rmtree(user, ignore_errors=True)
    sweep.load_fixture_module().create(args.source, user, "stage")
    command, env_extra = sweep.app_command(args, user, "EggStarGalaxy", 1)
    env = dict(os.environ, **env_extra)
    for key in ("PETARI_EDITOR_HOOKS_TEST", "PETARI_EDITOR_TERRAIN_TEST", "PETARI_STAGE_WARP", "PETARI_MOD_FOLDERS", "PETARI_OPEN_STAGE"):
        env.pop(key, None)
    env.update(PETARI_LOCK_CLASS="functional", PETARI_EDITOR="1", PETARI_EDITOR_NEWGALAXY_TEST="create")
    log = args.output / "new-galaxy.log"
    full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
    with log.open("wb") as stream:
        status = subprocess.run(full, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    result = (re.findall(r"PETARI SMOKE RESULT: (\w+)", text) or [None])[-1]
    lines = re.findall(r"\[editor-newgalaxy\] (.*)", text) + re.findall(r"\[editor\] (new galaxy.*)", text)
    print(f"new galaxy: exit {status}, smoke {result}")
    for line in lines:
        print(f"  {line}")
    mod = user / "mods" / "SmokeHopGalaxy"
    files = [mod / "galaxy.json", mod / "files/StageData/SmokeHopGalaxy.arc", mod / "files/StageData/SmokeHopGalaxy/SmokeHopGalaxyScenario.arc"]
    missing = [str(f.relative_to(user)) for f in files if not f.is_file()]
    if missing:
        print("  missing: " + ", ".join(missing))
    if not any(line.startswith("PASS") for line in lines) or result != "PASS" or missing:
        print("FAIL")
        return 1
    print("PASS: a new galaxy made in the editor, written as a mod, and Petari restarted into it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
