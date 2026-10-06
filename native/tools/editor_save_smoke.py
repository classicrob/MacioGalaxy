#!/usr/bin/env python3
"""Live check of saving an edit as a mod (docs/dev/level-editor/TECH.md 5.1: Full archives, then a patch).

Four launches of the stage smoke on EggStarGalaxy mission 1:

  1. editor off, PETARI_PLACEMENT_TRACE=Takobo   -> where the disc places each Octoomba
  2. PETARI_EDITOR=1, PETARI_MODS=LevelEditor, PETARI_EDITOR_SAVE_TEST=EditorSaveTest,
     PETARI_EDITOR_SAVE_FORM=full                -> native/editor/runtime/save_test.cpp enters Edit (Tab path),
                                                    moves an Octoomba 300 up and saves Full archives; the mod
                                                    folder must hold SoramameZone.arc (Yaz0) with that row
                                                    moved, mod.txt, and Folder.EditorSaveTest=on in mods.txt
  3. same user folder, the same but a patch      -> over the saved mod, the Octoomba moves 300 more and the mod
                                                    is saved as a patch: edits/EggStarGalaxy.json sets that
                                                    row's pos_y (disc-relative, both edits) and the full
                                                    archive is gone
  4. same user folder, editor off                -> the loader builds SoramameZone.arc from the patch, the game
                                                    opens it and places that Octoomba 600 units from run 1

usage: editor_save_smoke.py --app Petari.app/Contents/MacOS/Petari --output build/editor-save-smoke [--owner editorsave]
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stage_edit  # noqa: E402
import stage_sweep as sweep  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
STAGE, SCENARIO, MOD = "EggStarGalaxy", 1, "EditorSaveTest"


def run(args, name, user, extra, fresh=True):
    if fresh:
        shutil.rmtree(user, ignore_errors=True)
        sweep.load_fixture_module().create(args.source, user, "stage")
    log = args.output / f"{name}.log"
    command, env_extra = sweep.app_command(args, user, STAGE, SCENARIO)
    env = dict(os.environ, **env_extra)
    for key in ("PETARI_MODS", "PETARI_EDITOR", "PETARI_EDITOR_SAVE_TEST", "PETARI_EDITOR_SAVE_FORM", "PETARI_MOD_FOLDERS"):
        env.pop(key, None)
    env.update(PETARI_LOCK_CLASS="functional", PETARI_PLACEMENT_TRACE="Takobo", **extra)
    full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
    with log.open("wb") as stream:
        status = subprocess.run(full, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    return {
        "name": name, "status": status, "text": text,
        "result": (re.findall(r"PETARI SMOKE RESULT: (\w+)", text) or [None])[-1],
        "save": re.findall(r"\[editor-save\] (.*)", text),
        "takobo": {int(lid): tuple(map(float, pos)) for zone, lid, *pos in
                   re.findall(r"\[placement\] Takobo zone (\d+) l_id (-?\d+) at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)", text)
                   if zone == "14"},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01")
    parser.add_argument("--source", type=Path, default=ROOT / "build/observatory-user-2")
    parser.add_argument("--lock", type=Path, default=ROOT / "build/locked-app.sh")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--owner", default="editorsave")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--idle-frames", type=int, default=400)
    args = parser.parse_args()
    args.pipeline_policy = "default"
    args.output.mkdir(parents=True, exist_ok=True)
    failures = []

    disc = run(args, "1-disc", args.output / "1-user", {})
    user = args.output / "2-user"
    editor = {"PETARI_EDITOR": "1", "PETARI_MODS": "LevelEditor", "PETARI_EDITOR_SAVE_TEST": MOD}
    edited_re = r"edited (\w+) l_id (-?\d+) pos_y ([-\d.]+) -> ([-\d.]+)"
    save = run(args, "2-save-full", user, dict(editor, PETARI_EDITOR_SAVE_FORM="full"))
    edited = next((m for line in save["save"] for m in [re.match(edited_re, line)] if m), None)
    patched = None
    if "PASS" not in save["save"] or edited is None:
        failures.append(f"full save run: {save['save'][-2:]}")
    else:
        lid, after = int(edited.group(2)), float(edited.group(4))
        archive = user / "mods" / MOD / "files/StageData/SoramameZone.arc"
        if not archive.is_file():
            failures.append(f"no {archive}")
        else:
            raw = archive.read_bytes()
            edit = stage_edit.StageEdit(raw)
            table = edit.table("jmp/placement/layera/objinfo")
            rows = [r for r in range(len(table.rows)) if table.get(r, "l_id") == lid and table.get(r, "name") == "Takobo"]
            if raw[:4] != b"Yaz0" or not rows or abs(table.get(rows[0], "pos_y") - after) > 0.1:
                failures.append("the saved archive does not hold the edit")
        mods_txt = (user / "mods.txt").read_text() if (user / "mods.txt").is_file() else ""
        if f"Folder.{MOD}=on" not in mods_txt or not (user / "mods" / MOD / "mod.txt").is_file():
            failures.append("the mod is not switched on, or has no mod.txt")

        # Again over the saved mod, as a patch.
        patched = run(args, "3-save-patch", user, editor, fresh=False)
        again = next((m for line in patched["save"] for m in [re.match(edited_re, line)] if m), None)
        if "PASS" not in patched["save"] or again is None:
            failures.append(f"patch save run: {patched['save'][-2:]}")
        else:
            final = float(again.group(4))
            if abs(final - after - 300.0) > 0.1:
                failures.append(f"the second session did not start from the saved mod ({again.group(3)} != {after})")
            patch_file = user / "mods" / MOD / "edits/EggStarGalaxy.json"
            sets = []
            if patch_file.is_file():
                doc = json.loads(patch_file.read_text())
                sets = [op for op in doc["archives"].get("StageData/SoramameZone.arc", [])
                        if op["op"] == "set" and abs(op["cells"].get("pos_y", 1e9) - final) < 0.1]
            if not sets:
                failures.append("the patch does not set the Octoomba's pos_y")
            if archive.is_file():
                failures.append("the full archive is still in the mod after saving a patch")

        play = run(args, "4-mod-on", user, {}, fresh=False)
        if not re.search(r"\[mods\] patch mods: built StageData/SoramameZone.arc", play["text"]):
            failures.append("the loader did not build the patched archive")
        if not re.search(rf"\[mods\] game opened /StageData/SoramameZone.arc from mod {MOD}", play["text"]):
            failures.append("the game did not open the mod's archive")
        before, now = disc["takobo"].get(lid), play["takobo"].get(lid)
        moved = math.dist(before, now) if before and now else None
        print(f"Octoomba l_id {lid}: disc {before}, with the mod {now}, moved {moved}")
        if moved is None or abs(moved - 600.0) > 1.0:
            failures.append("the mod's Octoomba is not 600 units from the disc's (both saves)")
        # Again, in the editor over the patch mod the loader built (the editor reads it rebuilt from
        # the row edits, not the loader's copy: ModelIO::editingSource): one more move, saved as a patch.
        again2 = run(args, "5-save-over-patch", user, editor, fresh=False)
        third = next((m for line in again2["save"] for m in [re.match(edited_re, line)] if m), None)
        if "PASS" not in again2["save"] or third is None:
            failures.append(f"save over the patch: {again2['save'][-2:]}")
        elif abs(float(third.group(3)) - final) > 0.1:
            failures.append(f"the editor didn't start from the patch's edits ({third.group(3)} != {final})")
        for r in (disc, save, patched, play, again2):
            if r is not None and r["result"] != "PASS":
                failures.append(f"{r['name']}: stage smoke {r['result']}")
        for cache in user.glob("mods-cache/*"):
            print(f"  patch cache: {cache.name} ({sum(f.stat().st_size for f in cache.rglob('*') if f.is_file())} bytes)")
    for line in save["save"] + (patched["save"] if patched else []):
        print(f"  [editor-save] {line}")
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("PASS: the edit saved as Full archives, then as a patch over them, and the game built and loaded it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
