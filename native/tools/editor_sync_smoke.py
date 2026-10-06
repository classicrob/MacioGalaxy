#!/usr/bin/env python3
"""Live check of the level editor's SceneSync (docs/dev/level-editor/TECH.md 2.1, 2.8).

Two launches of the stage smoke on EggStarGalaxy mission 1, each in its own fixture user folder:

  1. editor off                          -> the smoke passes and nothing of the editor runs
  2. PETARI_EDITOR=1, PETARI_EDITOR_SYNC_TEST=1
                                         -> after the smoke's checks, native/editor/runtime/sync_test.cpp drives
                                            edit_api.hpp as the UI would: an op moves a placed Octoomba (its actor is
                                            re-created there), undo brings it back, a Coin is previewed and cancelled,
                                            and a start-point edit waits for a reload; it must log "[editor-sync] PASS"

usage: editor_sync_smoke.py --app Petari.app/Contents/MacOS/Petari --output build/editor-hooks-smoke [--owner editorsync]
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


def run(args, name, extra, stage=STAGE):
    user = args.output / f"{name}-user"
    shutil.rmtree(user, ignore_errors=True)
    sweep.load_fixture_module().create(args.source, user, "stage")
    log = args.output / f"{name}.log"
    command, env_extra = sweep.app_command(args, user, stage, SCENARIO)
    env = dict(os.environ, **env_extra)
    for key in ("PETARI_EDITOR", "PETARI_MODS", "PETARI_EDITOR_SYNC_TEST", "PETARI_EDITOR_HOOKS_TEST", "PETARI_EDITOR_REDRAW_TEST",
                "PETARI_EDITOR_ENSURE_TEST", "PETARI_EDITOR_SCRATCH_TEST"):
        env.pop(key, None)
    env.update(PETARI_LOCK_CLASS="functional", **extra)
    full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
    with log.open("wb") as stream:
        status = subprocess.run(full, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    return {
        "name": name, "status": status,
        "result": (re.findall(r"PETARI SMOKE RESULT: (\w+)", text) or [None])[-1],
        "hooks": re.findall(r"\[editor-(?:sync|redraw|ensure|scratch)\] (.*)", text),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01")
    parser.add_argument("--source", type=Path, default=ROOT / "build/observatory-user-2")
    parser.add_argument("--lock", type=Path, default=ROOT / "build/locked-app.sh")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--owner", default="editorsync")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--idle-frames", type=int, default=400)
    parser.add_argument("--only-redraw", action="store_true", help="run only the NPC re-create case")
    parser.add_argument("--only-scratch", action="store_true", help="run only the play-test isolation case")
    args = parser.parse_args()
    args.pipeline_policy = "default"
    args.output.mkdir(parents=True, exist_ok=True)

    if args.only_scratch:
        scratch = run(args, "5-scratch", {"PETARI_EDITOR": "1", "PETARI_EDITOR_SCRATCH_TEST": "1",
                                          "PETARI_EDITOR_SCRATCH_DUMP": str(args.output.resolve())})
        for line in scratch["hooks"]:
            print(f"  [editor-scratch] {line}")
        print("PASS" if "PASS" in scratch["hooks"] and scratch["result"] == "PASS" else f"FAIL: {scratch['hooks'][-3:]}")
        return 0 if "PASS" in scratch["hooks"] else 1
    if args.only_redraw:
        off = on = None
    else:
        off = run(args, "1-editor-off", {})
        on = run(args, "2-editor-on", {"PETARI_EDITOR": "1", "PETARI_EDITOR_SYNC_TEST": "1"})
    # An NPC (LOD models, a shadow): the gizmo drop's live re-create must draw it once.
    redraw = run(args, "3-redraw-rabbit", {"PETARI_EDITOR": "1", "PETARI_MODS": "LevelEditor", "PETARI_EDITOR_REDRAW_TEST": "Rabbit"}, "CosmosGardenGalaxy")
    # Runtime::ensureModel: an op applied in Play, Edit never entered (the agent tools' live mode).
    ensure = run(args, "4-ensure-model", {"PETARI_EDITOR": "1", "PETARI_EDITOR_ENSURE_TEST": "1"})
    # Play-test isolation: progress earned in a session is put back when it ends.
    scratch = run(args, "5-scratch", {"PETARI_EDITOR": "1", "PETARI_EDITOR_SCRATCH_TEST": "1"})
    failures = []
    if off is None:
        pass
    elif off["result"] != "PASS":
        failures.append(f"editor off: stage smoke {off['result']}")
    if off is not None and off["hooks"]:
        failures.append(f"editor off: editor hooks ran: {off['hooks'][:2]}")
    if on is not None and "PASS" not in on["hooks"]:
        failures.append(f"editor on: hooks check did not pass: {on['hooks'][-3:]}")
    if on is not None and on["result"] != "PASS":
        failures.append(f"editor on: stage smoke {on['result']}")
    if "PASS" not in redraw["hooks"]:
        failures.append(f"redraw: {redraw['hooks'][-3:]}")
    if "PASS" not in ensure["hooks"]:
        failures.append(f"ensureModel: {ensure['hooks'][-3:]}")
    if "PASS" not in scratch["hooks"]:
        failures.append(f"isolation: {scratch['hooks'][-3:]}")
    for r in [r for r in (off, on, redraw, ensure, scratch) if r is not None]:
        print(f"{r['name']}: exit {r['status']}, smoke {r['result']}")
        for line in r["hooks"]:
            print(f"  [editor-sync] {line}")
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("PASS: editor off unchanged; editor on: ops, undo and previews reach the live scene")
    return 0


if __name__ == "__main__":
    sys.exit(main())
