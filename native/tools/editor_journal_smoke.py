#!/usr/bin/env python3
"""Live check of the editor's journal: unsaved edits survive a crash (docs/dev/level-editor/UX.md, "Autosave and the journal").

Six launches of the stage smoke on EggStarGalaxy mission 1, one user folder, PETARI_EDITOR=1 and
PETARI_EDITOR_JOURNAL_TEST=<phase> (native/editor/runtime/journal_test.cpp):

  1. kill      -> Edit, an Octoomba 300 up, editor/journal/EggStarGalaxy.json holds 1 edit, SIGKILL
  2. restore   -> the offer is 1 edit; Restore reloads with the actor 300 up, unsaved; one more move is
                  journaled as 2 edits; Save (mod JournalTest) removes the journal
  3. kill      -> the same, now over the JournalTest patch mod (the loader's built archive)
  4. restore   -> the journal's archives come before the patch mod's (the actor 300 up from the mod's)
  5. kill
  6. discard   -> Discard removes the journal; the Octoomba stays put, nothing unsaved

usage: editor_journal_smoke.py --app Petari.app/Contents/MacOS/Petari --output build/editor-journal-smoke [--owner editorjournal]
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


def run(args, name, user, phase, fresh=False):
    if fresh:
        shutil.rmtree(user, ignore_errors=True)
        sweep.load_fixture_module().create(args.source, user, "stage")
    log = args.output / f"{name}.log"
    command, env_extra = sweep.app_command(args, user, STAGE, SCENARIO)
    env = dict(os.environ, **env_extra)
    for key in ("PETARI_MODS", "PETARI_EDITOR", "PETARI_EDITOR_JOURNAL_TEST", "PETARI_MOD_FOLDERS"):
        env.pop(key, None)
    env.update(PETARI_LOCK_CLASS="functional", PETARI_EDITOR="1", PETARI_MODS="LevelEditor", PETARI_EDITOR_JOURNAL_TEST=phase)
    full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
    with log.open("wb") as stream:
        status = subprocess.run(full, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    return {"name": name, "status": status, "text": text, "journal": re.findall(r"\[editor-journal\] (.*)", text),
            "result": (re.findall(r"PETARI SMOKE RESULT: (\w+)", text) or [None])[-1]}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01")
    parser.add_argument("--source", type=Path, default=ROOT / "build/observatory-user-2")
    parser.add_argument("--lock", type=Path, default=ROOT / "build/locked-app.sh")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--owner", default="editorjournal")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--idle-frames", type=int, default=400)
    args = parser.parse_args()
    args.pipeline_policy = "default"
    args.output.mkdir(parents=True, exist_ok=True)
    user = args.output / "user"
    journal = user / "editor/journal/EggStarGalaxy.json"
    failures = []
    runs = []
    for index, phase in enumerate(["kill", "restore", "kill", "restore", "kill", "discard"], 1):
        r = run(args, f"{index}-{phase}", user, phase, fresh=index == 1)
        runs.append(r)
        for line in r["journal"]:
            print(f"  {r['name']}: {line}")
        if phase == "kill":
            killed = any("killing" in line for line in r["journal"])
            if not killed or r["result"] is not None or r["status"] == 0:
                failures.append(f"{r['name']}: not killed after journaling (status {r['status']}, {r['journal'][-1:]})")
            elif not journal.is_file():
                failures.append(f"{r['name']}: no journal after the kill")
        else:
            if "PASS" not in r["journal"]:
                failures.append(f"{r['name']}: {r['journal'][-1:]}")
            if r["result"] != "PASS":
                failures.append(f"{r['name']}: stage smoke {r['result']}")
            if journal.is_file():
                failures.append(f"{r['name']}: the journal is still there")
        if failures:
            break
    shutil.rmtree(user / "cache", ignore_errors=True)
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("PASS: unsaved edits survived SIGKILL; Restore (over the disc and over a patch mod), Save and Discard clear the journal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
