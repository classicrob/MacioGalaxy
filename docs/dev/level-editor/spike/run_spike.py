#!/usr/bin/env python3
"""Editor spike runner (reference): Good Egg mission 1 stage smoke with PETARI_EDITOR_SPIKE=1.

Apply editor-spike.patch to a worktree, build `--target petari`, then:
  python3 run_spike.py <run-name>       (outputs under $BB_THREAD_STORAGE/spike/<run-name>)
Env: PETARI_EDITOR_SPIKE_NAMES=Takobo,Coin,SuperSpinDriver  PETARI_EDITOR_SPIKE_LOAD=Teresa,Karon
     PETARI_EDITOR_SPIKE_STRESS=50  PETARI_EDITOR_SPIKE_RELOAD=1  SPIKE_IDLE=2600
"""
import os, re, shutil, subprocess, sys
from pathlib import Path
MAIN = Path(__file__).resolve().parents[4]          # the main checkout (lock scripts, disc data, fixture source)
WT = Path(os.environ.get("SPIKE_TREE", MAIN.parent / "petari-worktrees/editor-spike"))  # the tree with the patch applied
sys.path.insert(0, str(MAIN / "native/tools"))
import stage_sweep as sweep  # noqa

class A: pass
args = A()
args.app = WT / "build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari"
args.disc = MAIN / "build/game-data/RMGE01"
args.frames = int(os.environ.get("SPIKE_FRAMES", "9000"))
args.idle_frames = int(os.environ.get("SPIKE_IDLE", "1100"))
args.pipeline_policy = "default"
name = sys.argv[1]
out = Path(os.environ["BB_THREAD_STORAGE"]) / "spike" / name
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
user = out / "user"
sweep.load_fixture_module().create(MAIN / "build/observatory-user-2", user, "stage")
command, env_extra = sweep.app_command(args, user, "EggStarGalaxy", 1)
env = dict(os.environ, **env_extra)
env.update(PETARI_LOCK_CLASS="functional", PETARI_EDITOR_SPIKE="1", PETARI_EDITOR_SPIKE_SHOTS=str(out / "shots"),
           PETARI_PLACEMENT_TRACE=os.environ.get("SPIKE_TRACE", "Takobo"))
(out / "shots").mkdir()
full = [str(MAIN / "build/locked-app.sh"), "editor-spike", "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", "900"] + command
with (out / "run.log").open("wb") as stream:
    status = subprocess.run(full, cwd=MAIN, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
text = (out / "run.log").read_text(errors="replace")
print("exit", status)
for line in text.splitlines():
    if "[editor-spike]" in line or "SMOKE RESULT" in line or "stage ready" in line or "Assertion" in line or "fault" in line.lower():
        print(line[:400])
