#!/usr/bin/env python3
"""Live check of New Galaxy in the editor (docs/dev/level-editor/new-galaxy-flow.md, M4).

One stage smoke on EggStarGalaxy mission 1 with PETARI_EDITOR=1 PETARI_EDITOR_NEWGALAXY_TEST=create
(native/editor/runtime/new_galaxy_test.cpp). Once the stage checks are done it asks for a new galaxy
("Smoke Hop": FlowerPlanet with its gravity, two missions, Flower Galaxy's music). The editor writes
it as a mod in the fixture user and restarts Petari into it: the same process (execv), so this run
sees both halves. The restarted game enters SmokeHopGalaxy (the stage fixture's entry, set by the
restart), runs the stage checks there and must log "[editor-newgalaxy] PASS". The smoke must pass,
and <user>/mods/SmokeHopGalaxy must hold galaxy.json and the two archives.

Run 2 is a player's restart: a copy of an unlocked save past the observatory (--saves; only read)
with run 1's galaxy mod, without the fixture, PETARI_OPEN_* set as the restart sets
them, PETARI_SMOKE=observe. The game must pass the strap and the title by itself, open file 1 without
the file select (milestone FileSelector.OpenFile) and arrive in SmokeHopGalaxy.

Run 3 creates "Smoke Box" from the Brushes template the same way (its box shown live, the editor on);
run 4 enters it with the editor off: the loader must bake the brush in the mod's own stage archive
([mods] terrain: lines) and the stage checks walk on the baked box.

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
    parser.add_argument("--saves", type=Path, default=ROOT / "build/saves/all-missions",
                        help="an unlocked save (NAND/) past the observatory, for the player's restart (only read)")
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

    # Run 2, the player's path: the same user without the test fixture, started as the restart
    # starts (PETARI_OPEN_*). The game must pass the strap, skip the title, open file 1 by itself
    # and go to the galaxy; the observe script only watches.
    # A save past the observatory (--stage only takes such a file): a copy of the unlocked
    # all-missions save, with the galaxy's mod from run 1, switched on as the editor switched it.
    user2 = args.output / "user-player"
    shutil.rmtree(user2, ignore_errors=True)
    shutil.copytree(args.saves / "NAND", user2 / "NAND")
    shutil.copytree(mod, user2 / "mods" / "SmokeHopGalaxy")
    (user2 / "mods.txt").write_text("Folder.SmokeHopGalaxy=on\n")
    command2 = [part for part in command if part not in ("--test-fixture", "stage")]
    command2[command2.index("--user") + 1] = str(user2)
    env2 = dict(env)
    for key in ("PETARI_STAGE", "PETARI_SCENARIO"):
        env2.pop(key, None)
    env2["PETARI_EDITOR_NEWGALAXY_TEST"] = "edit-check"  # the editor enters Edit by itself there
    env2["PETARI_MODS"] = "LevelEditor"  # on, as for whoever made the galaxy in the editor
    env2.update(PETARI_SMOKE="observe", PETARI_SMOKE_FRAMES="3000", PETARI_OPEN_STAGE="SmokeHopGalaxy", PETARI_OPEN_FILE="1",
                PETARI_OPEN_EDIT="1", PETARI_OPEN_TITLE="Smoke Hop")
    log2 = args.output / "player-open.log"
    full2 = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command2
    with log2.open("wb") as stream:
        status2 = subprocess.run(full2, cwd=ROOT, env=env2, stdout=stream, stderr=subprocess.STDOUT).returncode
    text2 = log2.read_text(errors="replace")
    result2 = (re.findall(r"PETARI SMOKE RESULT: (\w+)", text2) or [None])[-1]
    opened = "FileSelector.OpenFile" in text2
    arrived = re.search(r"observe: now in Game/SmokeHopGalaxy", text2) is not None
    editing = re.findall(r"\[editor-newgalaxy\] (.*)", text2)
    print(f"player open: exit {status2}, smoke {result2}; file opened by itself {opened}, in the galaxy {arrived}; {editing}")
    if result2 != "PASS" or not opened or not arrived or not any(line.startswith("PASS") for line in editing):
        print("FAIL")
        return 1

    # Run 3: the Brushes template ("Smoke Box": a checker box brush), created and entered with the
    # editor on (the box is shown live). Run 4: the same galaxy with the editor off, so the loader
    # bakes the brush inside the mod's own stage archive; the stage checks walk on the baked box.
    def fixture_run(name, extra, drop=()):
        cmd, extra_env = sweep.app_command(args, user, extra.pop("STAGE", "EggStarGalaxy"), 1)
        run_env = dict(os.environ, **extra_env)
        for key in ("PETARI_EDITOR_HOOKS_TEST", "PETARI_EDITOR_TERRAIN_TEST", "PETARI_STAGE_WARP", "PETARI_MOD_FOLDERS", "PETARI_OPEN_STAGE") + tuple(drop):
            run_env.pop(key, None)
        run_env.update(PETARI_LOCK_CLASS="functional", **extra)
        run_log = args.output / f"{name}.log"
        run_full = [str(args.lock), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + cmd
        with run_log.open("wb") as stream:
            code = subprocess.run(run_full, cwd=ROOT, env=run_env, stdout=stream, stderr=subprocess.STDOUT).returncode
        run_text = run_log.read_text(errors="replace")
        return code, (re.findall(r"PETARI SMOKE RESULT: (\w+)", run_text) or [None])[-1], run_text

    status3, result3, text3 = fixture_run("box-create", {"PETARI_EDITOR": "1", "PETARI_EDITOR_NEWGALAXY_TEST": "create-box"})
    lines3 = re.findall(r"\[editor-newgalaxy\] (.*)", text3)
    print(f"box create: exit {status3}, smoke {result3}")
    for line in lines3:
        print(f"  {line}")
    if result3 != "PASS" or not any(line.startswith("PASS") for line in lines3):
        print("FAIL")
        return 1
    status4, result4, text4 = fixture_run("box-editor-off", {"STAGE": "SmokeBoxGalaxy", "PETARI_EDITOR": "0"},
                                          drop=("PETARI_EDITOR_NEWGALAXY_TEST",))
    baked = re.findall(r"\[mods\] terrain: (.*)", text4)
    print(f"box, editor off: exit {status4}, smoke {result4}; baked by the loader: {baked}")
    if result4 != "PASS" or not baked:
        print("FAIL")
        return 1
    print("PASS: new galaxies made in the editor (one planet; brushes), written as mods and restarted into; a player's restart "
          "opens one without prompts; the loader bakes brushes inside a new galaxy's own archive")
    return 0


if __name__ == "__main__":
    sys.exit(main())
