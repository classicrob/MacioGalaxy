#!/usr/bin/env python3
"""Live check of the level editor's reload from the model and Play (docs/dev/level-editor/TECH.md 2.6, 2.7).

Two launches of the stage smoke on EggStarGalaxy mission 1, each in its own fixture user folder:

  1. editor off                          -> the smoke passes and nothing of the editor runs
  2. PETARI_EDITOR=1, PETARI_MODS=LevelEditor, PETARI_EDITOR_RELOAD_TEST=1 (Edit through the UI's Tab path)
                                         -> native/editor/runtime/reload_test.cpp: a live and a pending edit, Reload
                                            (the edited Octoomba comes back from the injected archive, nothing pending,
                                            the history kept, back in Edit), then Play from a marker (Mario there);
                                            it must log "[editor-reload] PASS"; the reload's time is printed

usage: editor_reload_smoke.py --app Petari.app/Contents/MacOS/Petari --output build/editor-hooks-smoke [--owner editorreload]
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


def run(args, name, extra):
    user = args.output / f"{name}-user"
    shutil.rmtree(user, ignore_errors=True)
    sweep.load_fixture_module().create(args.source, user, "stage")
    log = args.output / f"{name}.log"
    command, env_extra = sweep.app_command(args, user, STAGE, SCENARIO)
    env = dict(os.environ, **env_extra)
    for key in ("PETARI_MODS", "PETARI_EDITOR", "PETARI_EDITOR_RELOAD_TEST", "PETARI_EDITOR_SYNC_TEST", "PETARI_EDITOR_HOOKS_TEST"):
        env.pop(key, None)
    env.update(PETARI_LOCK_CLASS="functional", **extra)
    full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
    with log.open("wb") as stream:
        status = subprocess.run(full, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    return {
        "name": name, "status": status,
        "result": (re.findall(r"PETARI SMOKE RESULT: (\w+)", text) or [None])[-1],
        "hooks": re.findall(r"\[editor-reload\] (.*)", text) + re.findall(r"\[editor\] (reload.*)", text),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01")
    parser.add_argument("--source", type=Path, default=ROOT / "build/observatory-user-2")
    parser.add_argument("--lock", type=Path, default=ROOT / "build/locked-app.sh")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--owner", default="editorreload")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--idle-frames", type=int, default=1500)
    args = parser.parse_args()
    args.pipeline_policy = "default"
    args.output.mkdir(parents=True, exist_ok=True)

    off = run(args, "1-editor-off", {})
    on = run(args, "2-editor-on", {"PETARI_EDITOR": "1", "PETARI_MODS": "LevelEditor", "PETARI_EDITOR_RELOAD_TEST": "1"})
    failures = []
    if off["result"] != "PASS":
        failures.append(f"editor off: stage smoke {off['result']}")
    if off["hooks"]:
        failures.append(f"editor off: editor hooks ran: {off['hooks'][:2]}")
    if "PASS" not in on["hooks"]:
        failures.append(f"editor on: hooks check did not pass: {on['hooks'][-3:]}")
    if on["result"] != "PASS":
        failures.append(f"editor on: stage smoke {on['result']}")
    for r in (off, on):
        print(f"{r['name']}: exit {r['status']}, smoke {r['result']}")
        for line in r["hooks"]:
            print(f"  [editor-reload] {line}")
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("PASS: editor off unchanged; editor on: reload from the model and Play from the marker")
    return 0


if __name__ == "__main__":
    sys.exit(main())
