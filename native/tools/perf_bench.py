#!/usr/bin/env python3
"""Frame-time benchmark for the 60 fps work: quiet, focused, visible, warm (or cold) stage runs, and A/B.

Each run is one stage smoke (PETARI_SMOKE=stage, native/app/smoke_stage.hpp, the same synthetic entry as
stage_sweep.py) through build/locked-app.sh with PETARI_LOCK_CLASS=quiet (exclusive: every app slot, the sweep lane
and every build slot), PETARI_SMOKE_BACKGROUND=0 (a normal visible window that takes focus) and a fresh fixture user.
The measured window is the stage driver's gameplay script, ready -> end (~1,450 frames: idle, walks, jump, spin,
camera, pause menu). Numbers are GAME-THREAD CADENCE (seam-to-seam intervals from PETARI_FRAME_CSV, locked to the VI
retrace wait), not displayed frames; presents are immediate. When the frame CSV gains on-screen present columns
(wt/present-pacing), they are summarized as extra "present" columns automatically.

Caches. Metal keeps its shader cache in $(getconf DARWIN_USER_CACHE_DIR)/<bundle id>/, shared by every Petari launch
that is not a background smoke. Bench runs use an APFS clone of the app bundle whose Info.plist CFBundleIdentifier is
dev.petari.Petari.bench (Info.plist is not bound by the ad-hoc signature; the executable is untouched, so its sha256
stays the build identity): one private Metal path for every bench build, reset before each run.
  warm: one TEAM cache, <main>/build/perf-bench-warm/cache-g<generation>-<os build>-<chip>/ (`warm --print-path`):
        filled once by a foreground run with PETARI_PIPELINE_GLOBAL_PRECOMPILE=1 (the whole bundled global seed, about
        9,500 variants; functional owner perf-bench-warmup; ~11 min of MTLCompilerService at full load) plus optional
        stage visits. Every warm run starts from a fresh APFS clone of it, so runs do not warm each other. The entries are
        content-keyed (Metal, Dawn) or version-checked (Aurora), so another build can use it safely; changed shaders
        just compile (reported per run as slow compiles / skipped draws). Background-written Metal entries do NOT hit in
        foreground runs (measured), which is why the fill is foreground.
  cold: every run starts with an empty Metal directory and no pipeline/Dawn databases.
Pipeline policy, global precompile and stage gate are the app's defaults (what a player gets).

  perf_bench.py sets                                   # the stage sets
  perf_bench.py warm [--refresh] [--visit quick]       # fill the team warm cache now (else: on first warm run)
  perf_bench.py warm --print-path                      # where it is ('' if not filled)
  perf_bench.py run --set quick [--app APP] [--cold] [--repeat 2] [--name NAME]
  perf_bench.py run --stages HeavenlyBeachGalaxy:1,AstroGalaxy:2 --name repro
  perf_bench.py ab --a APP_A --b APP_B --set quick [--repeat 2] [--name NAME]   # interleaved A/B
  perf_bench.py run --drive goodegg1 --stages EggStarGalaxy:1 --frames 17000      # genuine play: event coverage
  perf_bench.py run --drive soak --stages HeavenlyBeachGalaxy:1                    # stage, exit, observatory, save, title
  perf_bench.py run --drive replay --route seg.csv --stages PhantomGalaxy:2        # a recorded route (recording_segments.py)
  perf_bench.py run --drive stage --stages BeltConveyerExGalaxy:1 --env PETARI_STAGE_WARP=26683.05,1300.0,-11987.62 \
      --env PETARI_SMOKE_QUIT_DELAY=1500                                           # star-get + return to the observatory
      (drives report r2r/busy/present over every gameplay frame and a spike list with the driver events around each)
  perf_bench.py run --warm-key NAME | ab --b-warm-key NAME   # a separately filled warm cache for a shader-changing build
  perf_bench.py report NAME [--vs OTHER_NAME]          # re-print tables (and a comparison)
  perf_bench.py clean [--all-warm] | clean --apps       # bench Metal dir (and warm caches; refuses while in use) | old app clones

APP is a Petari.app bundle or its Contents/MacOS/Petari (default: the main checkout's build). Output: <repo>/build/
perf-bench/<name>/ (results.json, summary.md, runs/<label>/<Stage>-s<n>-r<k>/{app.log,frames.csv,result.json}). The
default out root is the build/ of the checkout this script lives in (local disk; never ~/Space).
"""
import argparse
import csv
import datetime
import fcntl
import hashlib
import json
import os
import plistlib
import re
import shutil
import signal
import sqlite3
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parents[1]
sys.path.insert(0, str(TOOLS))
import stage_sweep as sweep  # noqa: E402

# Warm runs compile 0-11 pipelines slowly (>= 20 ms); above this the run did not start warm (NOT WARM, no A/B verdict).
NOT_WARM_SLOW = 50
FIELD_MS = 1000.0 * 1001.0 / 60000.0  # NTSC VI field, 16.683 ms
THRESHOLDS = (16.7, 20.8, 33.3)       # the brief's buckets: > one field (+jitter), late (1.25 fields), > two fields
LATE_MS = 20.8
OWNER = "perf-bench"

# Worst / most varied content (full135-v3 hints, which were concurrent and NOT evidence), one scenario each:
# water, caves, observatory, ghosts/fog, sand, ocean ring, the reference Good Egg, bees, a lava boss,
# the castle garden, a dome, lava. About 12 x 70 s.
QUICK = [
    ("HeavenlyBeachGalaxy", 1), ("OceanPhantomCaveGalaxy", 2), ("AstroGalaxy", 2), ("PhantomGalaxy", 3),
    ("SandClockGalaxy", 3), ("OceanRingGalaxy", 5), ("EggStarGalaxy", 1), ("HoneyBeeKingdomGalaxy", 3),
    ("KoopaBattleVs2Galaxy", 1), ("PeachCastleGardenGalaxy", 1), ("AstroDome", 5), ("HellProminenceGalaxy", 1),
]
EXCLUDE_FULL = {"EpilogueDemoStage"}  # the ending movie: no gameplay window

# Frame CSV parts (native/app/frame_stats.hpp). Game-thread parts are consecutive and sum to the interval.
GAME_THREAD = {
    "game_cpu": "game work minus the token-barrier and stage-prep waits inside it",
    "token_barrier": "game thread waits for the previous frame's GX tokens (inside game work)",
    "stage_prep": "stage shader preparation wait (inside game work)",
    "draw_done": "GXDrawDone wait + flush at frame end (GPU/GX FIFO catching up)",
    "gx_submit": "seam compose + aurora_end_frame (encode/submission side)",
    "frame_slot": "seam aurora_begin_frame (frame slot / staging memory wait, events)",
    "reacquire": "seam waiting to get the CPU baton back",
    "retrace": "waiting for the next VI retrace (idle; pacing)",
    "unattributed": "rest of the interval",
}
OTHER_THREADS = ("pipeline_wait", "drawable_acquire", "present_call", "frame_submit", "texture_upload",
                 "texture_hash", "efb_submit_wait", "efb_staging_wait")


def main_checkout():
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=REPO,
                            capture_output=True, text=True, check=True).stdout.strip()
    return Path(common).parent


MAIN = main_checkout()


def darwin_cache_dir():
    return Path(subprocess.run(["getconf", "DARWIN_USER_CACHE_DIR"], capture_output=True, text=True,
                               check=True).stdout.strip())


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def clone_tree(source, destination):
    if subprocess.run(["cp", "-cR", str(source), str(destination)], capture_output=True).returncode != 0:
        shutil.copytree(source, destination, symlinks=True)


def clone_file(source, destination):
    if subprocess.run(["cp", "-c", str(source), str(destination)], capture_output=True).returncode != 0:
        shutil.copyfile(source, destination)


def free_gib(path):
    st = os.statvfs(path)
    return st.f_bavail * st.f_frsize / 2**30


def log(text):
    print(f"[perf-bench] {text}", flush=True)


# --- app identity and the team warm cache ---------------------------------------------------------------------------

# Every bench app clone uses this bundle id, so every bench run reads and writes Metal's cache at one path,
# $(getconf DARWIN_USER_CACHE_DIR)/dev.petari.Petari.bench, reset from a snapshot before each run (under metal.lock).
# Measured 2026-10-17: Metal entries written by a background smoke (namespace-redirected cache path) did not hit in a
# foreground run (EggStar gate 2,050 pending, 12,766 skipped draws), while a foreground-written cache at the same path
# did (0 skipped draws). So the warm cache is written by foreground runs at exactly this path.
BENCH_ID = "dev.petari.Petari.bench"
# Bump to invalidate every team warm cache (e.g. after a broad shader generator change). Metal, Dawn and Aurora caches
# are content-addressed or version-checked, so a stale cache is never wrong, only colder: runs report slow compiles
# and skipped draws, and `warm --refresh` rebuilds it.
CACHE_GENERATION = 1


def cache_key():
    def sysctl(name):
        return subprocess.run(["sysctl", "-n", name], capture_output=True, text=True).stdout.strip()
    chip = re.sub(r"[^A-Za-z0-9]+", "", sysctl("machdep.cpu.brand_string"))
    return f"g{CACHE_GENERATION}-{sysctl('kern.osversion')}-{chip}"


class BenchApp:
    """An APFS clone of a Petari.app whose bundle id is BENCH_ID (the private Metal cache path)."""

    def __init__(self, app, warm_root, warm_key=None, cold=False):
        bundle = Path(app).resolve()
        while bundle.suffix != ".app" and bundle != bundle.parent:
            bundle = bundle.parent
        if bundle.suffix != ".app":
            raise SystemExit(f"perf_bench: {app} is not inside an .app bundle")
        self.source = bundle
        self.sha = sha256_of(bundle / "Contents/MacOS/Petari")
        # One Metal path per warm cache (and one for cold runs). MTLCompilerService processes live for days and serve
        # every client; when an A/B alternated two different snapshots at ONE path, ~10-20% of a "warm" run's
        # compiles missed (2026-10-17, ch-ab-warm-1: 660-908 slow compiles on 6 of 12 runs; same-snapshot sessions
        # stayed at 0). The team cache keeps the original path (dev.petari.Petari.bench) so its fill stays valid.
        if warm_key and not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,40}", warm_key):
            raise SystemExit(f"perf_bench: --warm-key {warm_key!r}: lowercase letters, digits, dashes")
        tag = "cold" if cold else (warm_key or "")
        self.bundle_id = BENCH_ID + (f"-{tag}" if tag else "")
        self.root = Path(warm_root) / "apps" / (self.sha[:16] + (f"-{tag}" if tag else ""))
        self.bundle = self.root / "Petari.app"
        self.exe = self.bundle / "Contents/MacOS/Petari"
        self.metal_dir = darwin_cache_dir() / self.bundle_id
        # warm_key: a separately filled warm cache for a build whose generated shader source differs from main's
        # (main's cache would make its "warm" runs compile cold). Same warm root, so metal.lock still serializes.
        self.warm_key = warm_key
        self.warm_cache = Path(warm_root) / (f"cache-{cache_key()}" + (f"-{warm_key}" if warm_key else ""))

    def freeze(self):
        """Clone the bundle (copy-on-write, ~no disk) and rewrite its bundle id. Idempotent."""
        self.root.mkdir(parents=True, exist_ok=True)
        if self.exe.is_file() and sha256_of(self.exe) == self.sha and \
                plistlib.loads((self.bundle / "Contents/Info.plist").read_bytes()).get("CFBundleIdentifier") == self.bundle_id:
            os.utime(self.root)
            return self
        partial = self.root / "Petari.app.partial"
        shutil.rmtree(partial, ignore_errors=True)
        shutil.rmtree(self.bundle, ignore_errors=True)
        clone_tree(self.source, partial)
        plist = partial / "Contents/Info.plist"
        info = plistlib.loads(plist.read_bytes())
        info["CFBundleIdentifier"] = self.bundle_id  # Info.plist is not bound by the ad-hoc linker signature
        plist.write_bytes(plistlib.dumps(info))
        if sha256_of(partial / "Contents/MacOS/Petari") != self.sha:
            raise SystemExit(f"perf_bench: {self.source} changed while it was copied (rebuild in progress?)")
        partial.rename(self.bundle)
        (self.root / "app.json").write_text(json.dumps({"source": str(self.source), "sha256": self.sha,
                                                        "bundle_id": self.bundle_id}, indent=2) + "\n")
        return self


def warm_ok(path, bundle_id=None):
    try:
        note = json.loads((path / "warm.json").read_text())
    except (OSError, ValueError):
        return False
    # The Metal snapshot belongs to the path it was filled at (fills before 2026-10-17 18:00: the shared BENCH_ID).
    if bundle_id is not None and note.get("bundle_id", BENCH_ID) != bundle_id:
        return False
    return (path / "metal").is_dir() and all((path / db).is_file() for db in ("pipeline_cache.db", "dawn_cache.db"))


def lock_command(args, inner_seconds, command, owner=None):
    lock = MAIN / "build/locked-app.sh"
    # Inside a batched bench the whole session already holds the app lock (see batch()).
    prefix = [str(lock), owner or args.owner] if lock.is_file() and not os.environ.get("PERF_BENCH_IN_LOCK") else []
    return prefix + ["/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(inner_seconds)] + command


def app_env(args, stage, scenario, extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith("PETARI_")}
    env.update({
        "PETARI_SMOKE": getattr(args, "drive", None) or "stage", "PETARI_STAGE": stage, "PETARI_SCENARIO": str(scenario),
        "PETARI_SMOKE_FRAMES": str(args.frames), "PETARI_STAGE_IDLE_FRAMES": str(args.idle_frames),
        "PETARI_SMOKE_BACKGROUND": "0",       # visible window, takes focus, real audio output
        "PETARI_FAST_BOOT": "1",              # strap screen accepts a button after 30 frames (boot only)
        "PETARI_TRACE_BOOT": "1", "PETARI_AUDIO_DIAG": "1",
        # Proposed (not yet in the app): drop physical input while keeping the focused window (no ASSISTED runs,
        # no Return-skips-preparation). Builds without it ignore the variable.
        "PETARI_SMOKE_IGNORE_PHYSICAL": "1",
    })
    drive = getattr(args, "drive", None)
    if drive == "soak":
        env["PETARI_SOAK_MINUTES"] = "1"  # the budget is checked after each stage part: 1 min = stage, exit, observatory, save, title, stage again (0 means 60): stage, back to the observatory, End Game with the save, title
    if drive == "replay":
        env["PETARI_REPLAY_ROUTE"] = str(Path(args.route).resolve())
    env.update(extra)
    return env


def launch(args, run_dir, mode, app, warm_metal, command, env, owner, timeout, log_path, metal_lock_held=False):
    """One app launch through the app lock and `_launch` (Metal reset under metal.lock). Returns (status, timed_out)."""
    spec = {"run_dir": str(run_dir), "mode": mode, "metal_dir": str(app.metal_dir), "warm_metal": str(warm_metal),
            "metal_lock": str(Path(args.warm_root) / "metal.lock"), "metal_lock_held": metal_lock_held,
            "command": command, "env": env}
    (run_dir / "spec.json").write_text(json.dumps(spec, indent=2) + "\n")
    inner = [sys.executable, "-I", str(Path(__file__).resolve()), "_launch", "--spec", str(run_dir / "spec.json")]
    with log_path.open("wb") as stream:
        process = subprocess.Popen(lock_command(args, timeout, inner, owner), cwd=REPO, env=env, stdout=stream,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        sampler = Sampler(command[command.index("--user") + 1])
        sampler.start()
        timed_out = False
        try:
            status = process.wait(timeout=timeout + args.lock_wait)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(process.pid, signal.SIGTERM)
            try:
                status = process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                status = process.wait()
        sampler.stop()
    return status, timed_out or status == 128 + signal.SIGALRM, sampler.summary()


def ensure_warm(args, app, refresh=False, fill_env=()):
    """The team warm cache (one per cache_key), filled first if missing: foreground runs of `app` under the
    functional owner perf-bench-warmup (a compile storm: MTLCompilerService near 400% for ~10 min)."""
    app.freeze()
    target = app.warm_cache
    if warm_ok(target, app.bundle_id) and not refresh:
        os.utime(target / "warm.json")
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    lock_script = MAIN / "build/locked-app.sh"
    if not os.environ.get("PERF_BENCH_IN_LOCK") and lock_script.is_file():
        # Lock order app lock -> metal.lock, as every run: the whole fill runs as one `warm` invocation inside one
        # app-lock hold (owner perf-bench-warmup), so it never holds metal.lock while queuing for the app lock.
        command = [str(lock_script), "perf-bench-warmup", sys.executable, "-I", str(Path(__file__).resolve()), "warm",
                   "--app", str(app.source), "--warm-root", str(args.warm_root), "--disc", str(args.disc),
                   "--source", str(args.source), "--force"]
        if app.warm_key:
            command += ["--warm-key", app.warm_key]
        if refresh:
            command.append("--refresh")
        if args.visit:
            command += ["--visit", ",".join(f"{s_}:{n}" for s_, n in args.visit)]
        for item in fill_env:
            command += ["--fill-env", item]
        env = dict(os.environ, PERF_BENCH_IN_LOCK="1", PETARI_LOCK_CLASS="functional",
                   PETARI_LOCK_TIMEOUT=str(1800 * (1 + len(args.visit)) + 600))
        status = subprocess.run(command, cwd=REPO, env=env).returncode
        if status != 0 or not warm_ok(target, app.bundle_id):
            raise SystemExit(f"perf_bench: warm fill for {target.name} failed (exit {status})")
        return
    with open(target.parent / "fill.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if warm_ok(target, app.bundle_id) and not refresh:
            return
        if free_gib(target.parent) < 45:
            raise SystemExit(f"perf_bench: only {free_gib(target.parent):.0f} GiB free; the 40 GB floor needs room")
        work = target.parent / f"fill-{os.getpid()}"
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir()
        user = work / "user"
        sweep.load_fixture_module().create(args.source, user, "stage")
        command = [str(app.exe), "--disc", str(args.disc), "--user", str(user), "--test-fixture", "stage"]
        visits = [("AstroGalaxy", 1)] + [v for v in args.visit if v != ("AstroGalaxy", 1)]
        log(f"warm fill {target.name} with {app.sha[:16]}: global precompile + {len(visits)} stage visit(s), "
            "visible window (do not press Return: it skips the preparation), owner perf-bench-warmup")
        started = time.time()
        prep = None
        # metal.lock from the first visit through the rename: between visits, another bench's reset or end-of-bench
        # cleanup must not touch the bench Metal dir the fill is building (2026-10-17: a finishing bench deleted it).
        metal_lock = open(Path(args.warm_root) / "metal.lock", "a")
        fcntl.flock(metal_lock, fcntl.LOCK_EX)
        for number, (stage, scenario) in enumerate(visits):
            extra = {"PETARI_LOCK_CLASS": "functional"}
            extra.update(item.split("=", 1) for item in fill_env)  # e.g. PETARI_PIPELINE_SEED_DIR for this cache
            if number == 0:
                # The blocking full preparation: every bundled global variant compiles before the game starts.
                extra["PETARI_PIPELINE_GLOBAL_PRECOMPILE"] = "1"
            visit_args = argparse.Namespace(**vars(args))
            visit_args.idle_frames = 120
            visit_args.drive, visit_args.frames = None, 12000  # the fill always uses the stage script
            env = app_env(visit_args, stage, scenario, extra)
            step = work / f"visit{number}"
            step.mkdir()
            status, timed_out, _ = launch(args, step, "cold" if number == 0 else "keep", app, "", command, env,
                                          "perf-bench-warmup", 1800, step / "app.log", metal_lock_held=True)
            text = (step / "app.log").read_text(errors="replace")
            log(f"  warm visit {stage}-s{scenario}: exit {status}{' (timeout)' if timed_out else ''}")
            if number == 0:
                found = re.findall(r"^\[gx startup prep\] result=(\w+) total=(\d+) remaining=(\d+) failed=(\d+)", text, re.M)
                prep = found[-1] if found else None
                if status != 0 or not prep or prep[0] != "ready":
                    raise SystemExit(f"perf_bench: warm fill failed (exit {status}, prep {prep}); see {step}/app.log")
            elif status != 0:
                log(f"  (visit failed; the cache is kept, the stage may be colder) see {step}/app.log")
        stage_dir = work / "cache"
        stage_dir.mkdir()
        try:
            app.metal_dir.rename(stage_dir / "metal")
        except BaseException:
            metal_lock.close()
            raise
        for db in ("pipeline_cache.db", "dawn_cache.db"):
            sweep.snapshot_sqlite(user / db, stage_dir / db)
        (stage_dir / "warm.json").write_text(json.dumps({
            "key": target.name, "bundle_id": app.bundle_id, "fill_env": list(fill_env), "filled_with": str(app.source), "sha256": app.sha,
            "filled": datetime.datetime.now().isoformat(timespec="seconds"), "seconds": round(time.time() - started),
            "prep_total": int(prep[1]), "prep_failed": int(prep[3]), "visits": [f"{s}-s{n}" for s, n in visits],
        }, indent=2) + "\n")
        old = target.with_name(target.name + f".old-{os.getpid()}")
        if target.exists():
            target.rename(old)
        stage_dir.rename(target)
        metal_lock.close()  # runs clone target/metal under metal.lock: swap first, then release
        shutil.rmtree(old, ignore_errors=True)
        shutil.rmtree(work, ignore_errors=True)
        log(f"warm cache ready: {target} ({prep[1]} variants, {time.time() - started:.0f} s)")
        prune(args)


def prune(args, keep_caches=4, app_days=3):
    """Keep the 4 most recently used warm caches and app clones used in the last 3 days (clones cost ~no disk)."""
    root = Path(args.warm_root)
    caches = sorted((p for p in root.glob("cache-*") if (p / "warm.json").is_file()),
                    key=lambda p: (p / "warm.json").stat().st_mtime, reverse=True)
    for old in caches[keep_caches:]:
        shutil.rmtree(old, ignore_errors=True)
        log(f"pruned warm cache {old.name}")
    prune_apps(root)


def prune_apps(warm_root, keep=3, recent_minutes=30, listing=None):
    """App clones (<warm-root>/apps/<sha16>): keep the `keep` most recently used, any used in the last
    `recent_minutes` (freeze and every run touch their clone, so a queued or long bench keeps its own), and any a
    running process executes. A rebuilt bundle shares no blocks with an older clone, so each old clone costs real
    disk (up to the bundle size, ~440 MB). Returns the removed names."""
    apps = Path(warm_root) / "apps"
    if not apps.is_dir():
        return []
    if listing is None:
        listing = subprocess.run(["ps", "-axo", "command="], capture_output=True, text=True).stdout
    clones = sorted((c for c in apps.iterdir() if c.is_dir()), key=lambda c: c.stat().st_mtime, reverse=True)
    removed = []
    for clone in clones[keep:]:
        if time.time() - clone.stat().st_mtime < recent_minutes * 60 or str(clone) + "/" in listing:
            continue
        shutil.rmtree(clone, ignore_errors=True)
        removed.append(clone.name)
    if removed:
        log(f"pruned {len(removed)} app clone(s): {', '.join(removed)}")
    return removed


# --- one run ----------------------------------------------------------------------------------------------------------

def cmd_launch(args):
    """Inside the app lock: snapshot lock state, reset the bench Metal dir, exec the app."""
    spec = json.loads(Path(args.spec).read_text())
    run_dir = Path(spec["run_dir"])
    slots = {}
    for d in sorted((MAIN / "build").glob(".macos-gx.lock*")):
        if d.is_dir() and (d / "owner").is_file():
            slots[d.name] = (d / "owner").read_text().strip()
    app_slots = {}
    for d in sorted((MAIN / "build").glob(".petari-app.lock*")):
        if d.is_dir() and (d / "owner").is_file():
            app_slots[d.name] = (d / "owner").read_text().strip()
    load = os.getloadavg()
    metal = Path(spec["metal_dir"])
    # Held until the app exits (the descriptor survives exec): one bench app at a time owns the bench Metal dir.
    # A background-window run (PETARI_SMOKE_BACKGROUND=1) keeps Metal in its own <user>/cache namespace: it neither
    # uses nor resets the shared bench Metal path, so it takes no metal.lock and runs beside other bench apps.
    background = spec["env"].get("PETARI_SMOKE_BACKGROUND") == "1"
    if background:
        spec["mode"] = "keep"
    if not spec.get("metal_lock_held") and not background:
        fd = os.open(spec["metal_lock"], os.O_CREAT | os.O_RDWR, 0o644)
        fcntl.flock(fd, fcntl.LOCK_EX)
        os.set_inheritable(fd, True)
    t0 = time.time()
    if spec["mode"] != "keep":
        shutil.rmtree(metal, ignore_errors=True)
    if spec["mode"] == "warm":
        clone_tree(spec["warm_metal"], metal)
    (run_dir / "launch.json").write_text(json.dumps({
        "build_slots": slots, "app_slots": app_slots, "loadavg": load, "reset_seconds": round(time.time() - t0, 2),
        "mode": spec["mode"], "started": time.time()}, indent=2) + "\n")
    os.execve(spec["command"][0], spec["command"], spec["env"])


class Sampler:
    """Every 5 s while the run's app is alive: other Petari apps, and CPU of other busy processes."""

    IGNORE = ("Petari", "MTLCompilerService", "WindowServer", "kernel_task", "ps", "perl", "python3", "Python")

    def __init__(self, user):
        self.user = str(user)
        self.samples = 0
        self.other_apps = 0
        self.other_cpu = []
        self.top = {}
        self.compiler_cpu, self.compiler_count, self.compiler_pri = [], [], set()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self._stop.wait(5.0):
            try:
                listing = subprocess.run(["ps", "-axo", "pid=,%cpu=,pri=,command="], capture_output=True, text=True,
                                         timeout=10).stdout
            except (OSError, subprocess.SubprocessError):
                continue
            apps, busy, compilers = [], [], []
            for line in listing.splitlines():
                parts = line.split(None, 3)
                if len(parts) < 4:
                    continue
                cpu, pri, command = float(parts[1]), parts[2], parts[3]
                if re.search(r"(^|/)(clang|clang\+\+|c\+\+|cc1plus)( |$)", command.split(" -")[0]) or " -cc1 " in command:
                    compilers.append((cpu, pri))
                exe = command.split(" ")[0]
                if exe.endswith("Petari.app/Contents/MacOS/Petari"):
                    apps.append(command)
                    continue
                name = os.path.basename(exe)
                if cpu >= 5.0 and not any(name.startswith(i) for i in self.IGNORE):
                    busy.append((cpu, name))
            if not any(self.user in a for a in apps):
                continue
            self.samples += 1
            self.other_apps = max(self.other_apps, sum(1 for a in apps if self.user not in a))
            self.other_cpu.append(sum(c for c, _ in busy))
            self.compiler_cpu.append(sum(c for c, _ in compilers))
            self.compiler_count.append(len(compilers))
            self.compiler_pri.update(p for _, p in compilers)
            for cpu, name in busy:
                self.top[name] = max(self.top.get(name, 0.0), cpu)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=15)

    def summary(self):
        top = sorted(self.top.items(), key=lambda kv: -kv[1])[:4]
        return {"samples": self.samples, "other_petari_apps": self.other_apps,
                "other_cpu_pct_mean": round(statistics.fmean(self.other_cpu), 1) if self.other_cpu else None,
                "other_cpu_pct_max": round(max(self.other_cpu), 1) if self.other_cpu else None,
                "other_cpu_top": [f"{n} {c:.0f}%" for n, c in top],
                # Compiler processes (a concurrent build): CPU sum, count and scheduling priorities (4 = darwin background).
                "compiler_cpu_pct_mean": round(statistics.fmean(self.compiler_cpu), 1) if self.compiler_cpu else None,
                "compiler_cpu_pct_max": round(max(self.compiler_cpu), 1) if self.compiler_cpu else None,
                "compilers_mean": round(statistics.fmean(self.compiler_count), 1) if self.compiler_count else None,
                "compiler_pri": sorted(self.compiler_pri)}


def run_once(args, out, label, app, stage, scenario, repeat, cold, variant_env=None, load_cmd=None):
    drive = getattr(args, "drive", None)
    name = (f"{drive}-" if drive else "") + f"{stage}-s{scenario}-r{repeat}"
    run_dir = out / "runs" / label / name
    if (run_dir / "result.json").is_file() and not args.rerun:
        log(f"skip {label}/{name}: result exists")
        return json.loads((run_dir / "result.json").read_text())
    shutil.rmtree(run_dir, ignore_errors=True)
    run_dir.mkdir(parents=True)
    os.utime(app.root)  # in use: prune_apps keeps recently used clones
    user = run_dir / "user"
    sweep.load_fixture_module().create(args.source, user, "stage")
    if not cold:
        for db in ("pipeline_cache.db", "dawn_cache.db"):
            clone_file(app.warm_cache / db, user / db)
    command = [str(app.exe), "--disc", str(args.disc), "--user", str(user), "--test-fixture", "stage"]
    extra = {"PETARI_FRAME_CSV": str(run_dir / "frames.csv"), "PETARI_LOCK_CLASS": args.lock_class}
    env_items = list(args.env) + list(variant_env or [])
    extra.update(item.split("=", 1) for item in env_items)
    env = app_env(args, stage, scenario, extra)
    repro = " ".join(f"{k}={v}" for k, v in env.items() if k.startswith("PETARI_")) + " " + " ".join(command)
    (run_dir / "command.txt").write_text(repro + "\n")
    log(f"run  {label}/{name} ({'cold' if cold else 'warm'})")
    started = time.time()
    load = None
    if load_cmd:
        # A deliberate concurrent load (ab --a-load/--b-load), e.g. a background-QoS build beside the run.
        load = subprocess.Popen(["/bin/bash", "-c", load_cmd], cwd=REPO, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        status, timed_out, machine = launch(args, run_dir, "cold" if cold else "warm", app, app.warm_cache / "metal",
                                            command, env, None, args.timeout, run_dir / "app.log")
    finally:
        if load is not None:
            try:
                os.killpg(load.pid, signal.SIGTERM)
                load.wait(timeout=20)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(load.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
    result = analyze_run(run_dir, user, stage, status, timed_out, drive)
    launched = {}
    try:
        launched = json.loads((run_dir / "launch.json").read_text())
    except (OSError, ValueError):
        pass
    slots = launched.get("build_slots", {})
    # .macos-gx.lock.bg: a PETARI_BUILD_BACKGROUND build (efficiency cores) allowed beside quiet runs; noted apart.
    background_build = {k: v for k, v in slots.items() if k.endswith(".bg")}
    foreign_build = {k: v for k, v in slots.items()
                     if not k.endswith(".bg") and v not in (args.owner, f"quiet-{args.owner}")}
    result.update({
        "label": label, "stage": stage, "scenario": scenario, "repeat": repeat, "mode": "cold" if cold else "warm",
        "drive": drive, "route": str(args.route) if drive == "replay" else None,
        "app": str(app.source), "app_sha256": app.sha, "lock_class": args.lock_class, "extra_env": env_items, "load_cmd": load_cmd,
        "wall_seconds": round(time.time() - started, 1),
        "app_seconds": round(time.time() - launched["started"], 1) if launched.get("started") else None,
        "lock": launched, "foreign_build_slots": foreign_build, "machine": machine,
    })
    ft = result.get("window") or {}
    contaminated = []
    if args.lock_class != "quiet":
        contaminated.append("not quiet")
    if load_cmd:
        contaminated.append("deliberate load (variant)")
    if background_build:
        contaminated.append(f"background build beside ({','.join(background_build.values())})")
    elif not load_cmd and (result["machine"].get("compiler_cpu_pct_mean") or 0) > 5:
        contaminated.append(f"compiler running beside (pri {','.join(result['machine']['compiler_pri'])})")
    if foreign_build:
        contaminated.append(f"build slot held by {','.join(foreign_build.values())}")
    if result["machine"]["other_petari_apps"]:
        contaminated.append("another Petari app ran")
    if not load_cmd and (result["machine"]["other_cpu_pct_mean"] or 0) > 300:  # 3 of 16 cores: an editor/agent desktop idles near 150%
        contaminated.append(f"other CPU {result['machine']['other_cpu_pct_mean']}%")
    slow = (result.get("shaders") or {}).get("slow_compiles") or 0
    result["not_warm"] = not cold and slow > NOT_WARM_SLOW
    if result["not_warm"]:
        contaminated.append(f"NOT WARM ({slow} slow compiles; stale or disturbed Metal cache: `warm --refresh`)")
    if ft.get("unfocused"):
        contaminated.append(f"{ft['unfocused']} unfocused frames")
    if ft.get("occluded"):
        contaminated.append(f"{ft['occluded']} occluded frames")
    result["contaminated"] = contaminated
    (run_dir / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    try:
        result["harvest"] = harvest_run(run_dir, user, app, env)
    except Exception as error:  # never lose a run over the harvest
        result["harvest"] = {"error": str(error)}
    (run_dir / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    # Disposable caches: the cloned databases and the reset-per-run Metal dir stay out of the run record.
    if not args.keep_user:
        for db in list(user.glob("dawn_cache.db*")) + list(user.glob("pipeline_cache.db*")):
            db.unlink()
        shutil.rmtree(user / "NAND", ignore_errors=True)  # the fixture save copy (8.7 MB per run); crash reports stay
        shutil.rmtree(user / "cache", ignore_errors=True)  # a background run's private Metal/Dawn caches
    w = result.get("window") or {}
    q = w.get("r2r") or {}
    log(f"done {label}/{name}: {result['outcome']} r2r p99 {q.get('p99')} max {q.get('max')} late {q.get('over_20.8')}; "
        f"busy p99 {(w.get('busy') or {}).get('p99')}; interval p99 {w.get('p99')}; present {present_cell(result)}; "
        f"late phase {w.get('dominant')} ({result['wall_seconds']} s)"
        + (f" CONTAMINATED: {'; '.join(contaminated)}" if contaminated else ""))
    return result


# --- seed harvest -----------------------------------------------------------------------------------------------------

STAGE_CONFIG = re.compile(r"^\[gx stage config\] stage=(\S+) config=([0-9a-f]{16})", re.M)
MISS = re.compile(r"^\[gx pipeline miss\] key=\w+ config=([0-9a-f]{16}) skipped=(\d+)", re.M)


def u64(value):
    return value & (2**64 - 1)


def seed_dir_for(app_bundle, env):
    override = env.get("PETARI_PIPELINE_SEED_DIR")
    return Path(override) if override else Path(app_bundle) / "Contents/Resources/pipeline-seeds"


def harvest_run(run_dir, user, app, env):
    """Configs this run used that the seeds it ran with lack: their rows (from the run's own pipeline_cache.db) go to
    run_dir/harvest.db, their stages ([gx stage config] lines) and skipped-draw misses to harvest.json. Real play
    reaches configs idle sweeps never do; a player otherwise meets them as pop-in even after a full warm-up."""
    db = next((p for p in (user / "cache/pipeline_cache.db", user / "pipeline_cache.db") if p.is_file()), None)
    global_db = seed_dir_for(app.bundle, env) / "__global__.db"
    if db is None or not global_db.is_file():
        return {"error": f"no run database ({db}) or global seed ({global_db})"}
    seeded = {u64(h) for (h,) in sqlite3.connect(f"file:{global_db}?immutable=1", uri=True).execute(
        "select hash from pipeline_cache")}
    text = (run_dir / "app.log").read_text(errors="replace")
    stages = {}
    for stage, config in STAGE_CONFIG.findall(text):
        if int(config, 16) not in seeded:
            stages.setdefault(config, set()).add(stage)
    misses = {c: int(n) for c, n in MISS.findall(text)}
    source = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    schema = source.execute("select value from aurora_schema").fetchone()
    rows = [r for r in source.execute("select type,hash,config_version,config_size,config,first_frame_used "
                                      "from pipeline_cache") if u64(r[1]) not in seeded]
    source.close()
    out = run_dir / "harvest.db"
    out.unlink(missing_ok=True)
    target = sqlite3.connect(out)
    with target:
        target.executescript("CREATE TABLE aurora_schema(value INTEGER);"
                             "CREATE TABLE pipeline_cache(type INTEGER NOT NULL, hash INTEGER NOT NULL, config_version "
                             "INTEGER NOT NULL, config_size INTEGER NOT NULL, config BLOB NOT NULL, first_frame_used "
                             "INTEGER NOT NULL, PRIMARY KEY(type, hash));")
        target.execute("insert into aurora_schema values (?)", schema)
        target.executemany("insert into pipeline_cache values (?,?,?,?,?,?)", rows)
    target.close()
    note = {"seeds": str(global_db.parent), "rows": len(rows), "run_stage": env.get("PETARI_STAGE"),
            "stages": {c: sorted(v) for c, v in sorted(stages.items())},
            "misses": misses, "misses_unseeded": sorted(c for c in misses if int(c, 16) not in seeded),
            "rows_without_stage": sorted(f"{u64(r[1]):016x}" for r in rows if f"{u64(r[1]):016x}" not in stages)}
    (run_dir / "harvest.json").write_text(json.dumps(note, indent=2) + "\n")
    return {k: (len(v) if isinstance(v, (dict, list)) else v) for k, v in note.items()}


def cmd_harvest(args):
    """Merge the harvests of benches (run dirs with harvest.db/json) into OUT: harvest.db (all rows), harvest.json
    (config -> stages, runs, skipped draws), and with --seed-dir a test seed dir: the given seeds plus the rows, in
    __global__.db and in each attributed <Stage>.db, hot (first_frame_used 0) so gates and backlogs take them first."""
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rows, info = {}, {}
    schema = None
    for bench in args.benches:
        for note_path in sorted(Path(bench).glob("runs/*/*/harvest.json")):
            note = json.loads(note_path.read_text())
            db = note_path.with_name("harvest.db")
            con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
            schema = schema or con.execute("select value from aurora_schema").fetchone()
            for row in con.execute("select type,hash,config_version,config_size,config,first_frame_used from pipeline_cache"):
                rows.setdefault((row[0], row[1]), row)
            con.close()
            run = str(note_path.parent)
            # The run's stage (older harvests: from the run dir name, <drive>-<Stage>-s<n>-r<k>).
            run_stage = note.get("run_stage") or re.sub(r"^(?:[a-z0-9]+-)?(\w+?)-s\d+-r\d+$", r"\1", note_path.parent.name)
            for config in note.get("misses_unseeded", []):
                # A miss was really drawn where it missed: with no [gx stage config] line, attribute it to the run's stage.
                if not note["stages"].get(config):
                    note["stages"][config] = [run_stage]
            for config, stages in note["stages"].items():
                entry = info.setdefault(config, {"stages": set(), "runs": set(), "skipped": 0})
                entry["stages"].update(stages)
                entry["runs"].add(run)
            for config, skipped in note["misses"].items():
                entry = info.setdefault(config, {"stages": set(), "runs": set(), "skipped": 0})
                entry["skipped"] += skipped
                entry["runs"].add(run)
    by_hash = {f"{u64(key[1]):016x}": key for key in rows}
    merged = out / "harvest.db"
    merged.unlink(missing_ok=True)
    con = sqlite3.connect(merged)
    with con:
        con.executescript("CREATE TABLE aurora_schema(value INTEGER);"
                          "CREATE TABLE pipeline_cache(type INTEGER NOT NULL, hash INTEGER NOT NULL, config_version "
                          "INTEGER NOT NULL, config_size INTEGER NOT NULL, config BLOB NOT NULL, first_frame_used "
                          "INTEGER NOT NULL, PRIMARY KEY(type, hash));")
        if schema:
            con.execute("insert into aurora_schema values (?)", schema)
        con.executemany("insert into pipeline_cache values (?,?,?,?,?,?)", rows.values())
    con.close()
    summary = {c: {"stages": sorted(e["stages"]), "runs": len(e["runs"]), "skipped_draws": e["skipped"],
                   "row": c in by_hash} for c, e in sorted(info.items(), key=lambda kv: -kv[1]["skipped"])}
    (out / "harvest.json").write_text(json.dumps(summary, indent=2) + "\n")
    # For order_pipeline_seeds.py add-rows (cold-hitch): config -> stages, and skipped draws as ranking weights.
    (out / "stages.json").write_text(json.dumps({
        "stages": {c: e["stages"] for c, e in summary.items() if e["row"] and e["stages"]},
        "weights": {c: e["skipped_draws"] for c, e in summary.items() if e["row"] and e["skipped_draws"]},
    }, indent=2) + "\n")
    log(f"harvest: {len(rows)} rows, {len(summary)} configs ({sum(1 for v in summary.values() if v['skipped_draws'])} "
        f"with skipped draws) -> {out}")
    if args.seed_dir:
        seeds = out / "seeds"
        shutil.rmtree(seeds, ignore_errors=True)
        shutil.copytree(args.seed_dir, seeds)
        def add(db_path, wanted):
            con = sqlite3.connect(db_path)
            with con:
                con.executemany("insert or ignore into pipeline_cache values (?,?,?,?,?,0)",
                                [rows[by_hash[c]][:5] for c in wanted if c in by_hash])
            con.close()
        add(seeds / "__global__.db", list(by_hash))
        per_stage = {}
        for config, entry in summary.items():
            for stage in entry["stages"]:
                per_stage.setdefault(stage, []).append(config)
        for stage, configs in sorted(per_stage.items()):
            if (seeds / f"{stage}.db").is_file():
                add(seeds / f"{stage}.db", configs)
            else:
                log(f"  no seed database for stage {stage}: its {len(configs)} configs are in __global__ only")
        log(f"test seed dir: {seeds} (use with --env PETARI_PIPELINE_SEED_DIR={seeds})")


# --- analysis ---------------------------------------------------------------------------------------------------------

def num(row, key):
    value = row.get(key)
    try:
        return float(value) if value not in (None, "") else 0.0
    except ValueError:
        return 0.0


def pct(values, fraction):
    return sweep.percentile(values, fraction)


SEAM = ("seam_compose_ms", "seam_end_frame_ms", "seam_begin_frame_ms", "seam_reacquire_ms")


def fc_pct(ordered, q):
    """frame-cost's retrace_pace.py indexing (v[int(q*(n-1))]), so the numbers agree."""
    return ordered[int(q * (len(ordered) - 1))]


def frame_parts(prev, row):
    """Game-thread parts of a retrace-to-retrace span: the previous row's work after its retrace (game work, draw-done,
    rest), then this row's seam and retrace wait (the span ends at this row's retrace)."""
    game_work = num(prev, "game_work_ms")
    token, prep = num(prev, "token_barrier_wait_ms"), num(prev, "stage_prep_wait_ms")
    values = {
        "game_cpu": max(0.0, game_work - token - prep), "token_barrier": token, "stage_prep": prep,
        "draw_done": num(prev, "draw_done_wait_ms"), "unattributed": num(prev, "unattributed_ms"),
        "gx_submit": num(row, "seam_compose_ms") + num(row, "seam_end_frame_ms"),
        "frame_slot": num(row, "seam_begin_frame_ms"), "reacquire": num(row, "seam_reacquire_ms"),
        "retrace": num(row, "retrace_wait_ms"),
    }
    for key in OTHER_THREADS:  # other threads, overlapping; both rows' share of the span
        values[key] = num(prev, key + "_ms") + num(row, key + "_ms")
    return values


def breakdown(spans):
    parts = {}
    for values in spans:
        for key, value in values.items():
            parts[key] = parts.get(key, 0.0) + value
    if not spans:
        return {"n": 0, "dominant": "-"}
    costs = {k: parts[k] for k in GAME_THREAD if k != "retrace"}  # retrace wait is idle time, not cost
    dominant = max(costs, key=costs.get)
    return {"n": len(spans), "mean_ms": {k: round(v / len(spans), 2) for k, v in parts.items()},
            "dominant": dominant, "dominant_share": round(costs[dominant] / max(1e-9, sum(costs.values())), 2)}


def window_stats(rows, first, last, display_hz=None):
    """Gameplay window (first, last].

    Primary pacing metric: retrace-to-retrace (r2r), the time between consecutive frame-loop retrace marks
    (seam entry + seam parts + retrace wait), as frame-cost's retrace_pace.py: it is what the VI cadence sees, while the
    seam-to-seam interval jitters with per-frame work even when no retrace is missed. Headroom: busy = interval -
    retrace wait. The raw interval stats are kept alongside. Late spans (r2r > 20.8 ms) get a phase breakdown."""
    frames = [r for r in rows if first < int(r["frame"]) <= last]
    if not frames:
        return None
    intervals = [num(r, "interval_ms") for r in frames]
    stats = {
        "frames": len(frames), "mean": round(statistics.fmean(intervals), 2),
        "p50": round(pct(intervals, 0.50), 2), "p95": round(pct(intervals, 0.95), 2),
        "p99": round(pct(intervals, 0.99), 2), "max": round(max(intervals), 2),
        "unfocused": sum(1 for r in frames if r.get("unfocused") == "1"),
        # Window covered (frames.csv `occluded`, when the build has it): display numbers then mean nothing.
        "occluded": sum(1 for r in frames if r.get("occluded") == "1") if "occluded" in frames[0] else None,
    }
    for t in THRESHOLDS:
        stats[f"over_{t}"] = sum(1 for v in intervals if v > t)
    r2r, late_spans = [], []
    for prev, row in zip(frames, frames[1:]):
        if prev.get("phase") != "gameplay" or row.get("phase") != "gameplay":
            continue
        span = num(prev, "interval_ms") + sum(num(row, c) for c in SEAM) + num(row, "retrace_wait_ms") \
            - sum(num(prev, c) for c in SEAM) - num(prev, "retrace_wait_ms")
        r2r.append(span)
        if span > LATE_MS:
            late_spans.append(frame_parts(prev, row))
    if r2r:
        v = sorted(r2r)
        stats["r2r"] = {"n": len(v), "p50": round(fc_pct(v, .5), 2), "p99": round(fc_pct(v, .99), 2),
                        "max": round(v[-1], 2), "over_17.5": sum(x > 17.5 for x in v),
                        "over_20.8": sum(x > 20.8 for x in v), "over_33.3": sum(x > 33.3 for x in v)}
    busy = sorted(num(r, "interval_ms") - num(r, "retrace_wait_ms") for r in frames if r.get("phase") == "gameplay")
    if busy:
        stats["busy"] = {"p50": round(busy[len(busy) // 2], 2), "p95": round(busy[int(.95 * len(busy))], 2),
                         "p99": round(busy[int(.99 * len(busy))], 2), "max": round(busy[-1], 2)}
    # Phase of the late retrace spans; without any, of the frames whose seam interval exceeded 20.8 ms.
    late = breakdown(late_spans)
    late["basis"] = "r2r > 20.8"
    if not late_spans:
        pairs = [(p, r) for p, r in zip(frames, frames[1:]) if num(r, "interval_ms") > LATE_MS]
        late = breakdown([frame_parts(p, r) for p, r in pairs])
        late["basis"] = "interval > 20.8 (no late retrace)"
    stats["late"] = late
    stats["slow_frames"], stats["dominant"] = late["n"], late["dominant"]
    stats["dominant_share"] = late.get("dominant_share")
    stats["slow_mean_ms"] = late.get("mean_ms", {})
    stats["drawable_acquire_mean"] = round(statistics.fmean(num(r, "drawable_acquire_ms") for r in frames), 3)
    # On-screen presentation (frames.csv present_ms / present_state, native/gx/present/README.md): present_ms is
    # this frame's drawable vs the previous presented one, empty unless the drawable was presented.
    if "present_state" in frames[0]:
        states = {}
        for r in frames:
            states[r.get("present_state") or "none"] = states.get(r.get("present_state") or "none", 0) + 1
        values = [num(r, "present_ms") for r in frames if r.get("present_ms") not in (None, "")]
        present = {"states": states}
        if values:
            present.update({"n": len(values), "p50": round(pct(values, .5), 2), "p99": round(pct(values, .99), 2),
                            "max": round(max(values), 2), "over_20.8": sum(1 for v in values if v > LATE_MS)})
            if display_hz:
                # As the exit line: refresh ticks per interval, off-cadence = not within 2 ms of a tick multiple.
                period = 1000.0 / display_hz
                ticks = [0, 0, 0, 0]
                off = 0
                for v in values:
                    n = max(1, round(v / period))
                    ticks[min(n, 4) - 1] += 1
                    off += abs(v - n * period) > 2.0
                present.update({"ticks_1_2_3_4plus": ticks, "off_cadence": off, "display_hz": display_hz})
        stats["present"] = present
    return stats


def analyze_run(run_dir, user, stage, status, timed_out, drive=None):
    base = sweep.analyze(run_dir / "app.log", run_dir / "frames.csv", stage, status, timed_out, user)
    base["outcome"], base["signature"] = sweep.classify(base)
    frames = base.get("frames") or {}
    result = {"outcome": base["outcome"], "signature": base.get("signature"), "exit_status": status,
              "timed_out": timed_out, "frames": frames, "load_to_ready_seconds": base.get("load_to_ready_seconds")}
    sh = base.get("shaders") or {}
    result["shaders"] = {"compiles": sh.get("compiles"), "cache_hits": sh.get("compile_cache_hits"),
                         "slow_compiles": (sh.get("compiles") or 0) - (sh.get("compile_cache_hits") or 0),
                         "build_ms_total": sh.get("compile_build_ms_total"), "policy": sh.get("pipeline_policy"),
                         "stage_gate": sh.get("stage_gate")}
    text = (run_dir / "app.log").read_text(errors="replace") if (run_dir / "app.log").is_file() else ""
    summary = re.findall(r"^\[gx pipeline summary\].*$", text, re.M)
    result["pipeline_summary"] = summary[-1] if summary else None
    skipped = re.findall(r"skipped_draws=(\d+)", summary[-1]) if summary else []
    result["skipped_draws"] = int(skipped[0]) if skipped else None
    present = re.findall(r"^Petari present intervals \(on-screen, (\d+)\): p50 ([\d.]+) p95 ([\d.]+) p99 ([\d.]+) "
                         r"max ([\d.]+) ms; off-cadence .*? (\d+) \(([\d.]+)%\); intervals of 1/2/3/4\+ refreshes "
                         r"(\d+)/(\d+)/(\d+)/(\d+); display ([\d.]+) Hz; present mode (\w+); drawables (\d+), "
                         r"presented (\d+), dropped (\d+), never presented (\d+)", text, re.M)
    if present:
        v = present[-1]
        result["present_exit"] = {"n": int(v[0]), "p50": float(v[1]), "p95": float(v[2]), "p99": float(v[3]),
                                  "max": float(v[4]), "off_cadence": int(v[5]), "off_cadence_pct": float(v[6]),
                                  "ticks_1_2_3_4plus": [int(x) for x in v[7:11]], "display_hz": float(v[11]),
                                  "mode": v[12], "drawables": int(v[13]), "presented": int(v[14]),
                                  "dropped": int(v[15]), "never_presented": int(v[16])}
        tail = re.findall(r"^Petari present intervals .*?maximumDrawableCount (\d+).*?; occluded at acquire (\d+), "
                          r"presented while occluded (\d+)", text, re.M)
        if tail:
            result["present_exit"].update({"max_drawables": int(tail[-1][0]), "occluded_at_acquire": int(tail[-1][1]),
                                           "presented_while_occluded": int(tail[-1][2])})
    elif "Petari present intervals: no drawables observed" in text:
        result["present_exit"] = {"n": 0}
    latency = [float(x) for x in re.findall(r"^\[latency\] press: .*? shown ([\d.]+) ms", text, re.M)]
    if latency:
        result["latency_shown_ms"] = {"n": len(latency), "p50": round(pct(latency, .5), 1), "max": round(max(latency), 1)}
    cache = re.findall(r"^\[gx pipeline cache\].*$", text, re.M)
    result["cache_line"] = cache[-1] if cache else None
    csv_path = run_dir / "frames.csv"
    if csv_path.is_file() and not drive and frames.get("ready") and frames.get("end"):
        with csv_path.open() as handle:
            rows = list(csv.DictReader(handle))
        hz = (result.get("present_exit") or {}).get("display_hz")
        result["window"] = window_stats(rows, frames["ready"], frames["end"], hz)
        if frames.get("load_start"):
            loading = window_stats(rows, frames["load_start"], frames["ready"])
            if loading:
                result["loading"] = {k: loading[k] for k in ("frames", "p50", "p99", "max", "over_33.3")}
    elif csv_path.is_file() and drive:
        # A gameplay driver (goodegg1, soak, replay): the window is the whole run from the first gameplay frame; the
        # pacing metrics (r2r, busy) count gameplay frames only, the spike list covers every phase.
        with csv_path.open() as handle:
            rows = list(csv.DictReader(handle))
        first = next((int(r["frame"]) for r in rows if r.get("phase") == "gameplay"), None)
        hz = (result.get("present_exit") or {}).get("display_hz")
        if first is not None:
            result["window"] = window_stats(rows, first - 1, int(rows[-1]["frame"]), hz)
            result["window"]["first_gameplay_frame"] = first
            phases = {}
            for r in rows:
                if int(r["frame"]) >= first:
                    phases[r.get("phase")] = phases.get(r.get("phase"), 0) + 1
            result["window"]["phase_frames"] = phases
            result["spikes"] = spike_list(rows, first, events_from_log(text), hz)
        else:
            result["window"] = None
    else:
        result["window"] = None
    if drive:
        result["events"] = len(events_from_log(text))
    return result


EVENT = re.compile(r"^PETARI SMOKE \[frame (\d+)\]: (.*)$", re.M)


def events_from_log(text):
    """The driver's own log lines, by frame (the frame CSV's index): planets, launches, talks, star, save, deaths."""
    return [(int(f), line.strip()[:160]) for f, line in EVENT.findall(text)]


def row_parts(row):
    """Game-thread parts of one seam-to-seam frame, and the other-thread waits beside it."""
    game_work, token, prep = num(row, "game_work_ms"), num(row, "token_barrier_wait_ms"), num(row, "stage_prep_wait_ms")
    parts = {"game_cpu": max(0.0, game_work - token - prep), "token_barrier": token, "stage_prep": prep,
             "draw_done": num(row, "draw_done_wait_ms"), "gx_submit": num(row, "seam_compose_ms") + num(row, "seam_end_frame_ms"),
             "frame_slot": num(row, "seam_begin_frame_ms"), "reacquire": num(row, "seam_reacquire_ms"),
             "unattributed": num(row, "unattributed_ms")}
    return parts


def describe(parts, others):
    main = sorted(parts.items(), key=lambda kv: -kv[1])[:2]
    text = ", ".join(f"{k} {v:.1f}" for k, v in main if v >= 0.5)
    extra = ", ".join(f"{k} {v:.1f}" for k, v in others.items() if v >= 1.0)
    return text + (f" (beside: {extra})" if extra else "")


def spike_list(rows, first, events, hz=None, limit=20):
    """Worst moments after the first gameplay frame: late retrace spans (gameplay), long frames in any phase
    (> 33.3 ms: a visible stall, e.g. a loading or menu animation), and on-screen present gaps >= 33 ms. Each with its
    phase breakdown and the driver events around it."""
    spikes = []
    rows = [r for r in rows if int(r["frame"]) >= first - 1]
    for prev, row in zip(rows, rows[1:]):
        frame = int(row["frame"])
        others = {k: num(row, k + "_ms") for k in ("pipeline_wait", "texture_upload", "drawable_acquire", "efb_submit_wait")}
        if prev.get("phase") == "gameplay" and row.get("phase") == "gameplay":
            span = num(prev, "interval_ms") + sum(num(row, c) for c in SEAM) + num(row, "retrace_wait_ms") \
                - sum(num(prev, c) for c in SEAM) - num(prev, "retrace_wait_ms")
            if span > LATE_MS:
                parts = {k: v for k, v in frame_parts(prev, row).items() if k in GAME_THREAD and k != "retrace"}
                spikes.append({"frame": frame, "kind": "late retrace", "ms": round(span, 2), "phase": "gameplay",
                               "cause": describe(parts, others)})
                continue
        interval = num(row, "interval_ms")
        if interval > 33.3:
            spikes.append({"frame": frame, "kind": "long frame", "ms": round(interval, 2), "phase": row.get("phase"),
                           "cause": describe(row_parts(row), others)})
        if row.get("present_state") == "ok" and num(row, "present_ms") >= 33.0:
            spikes.append({"frame": frame, "kind": "present gap", "ms": round(num(row, "present_ms"), 2),
                           "phase": row.get("phase"), "cause": f"{round(num(row, 'present_ms') * (hz or 60) / 1000)} refreshes"})
    spikes.sort(key=lambda s: -s["ms"])
    frames_of = [f for f, _ in events]
    import bisect
    for spike in spikes[:limit]:
        i = bisect.bisect_right(frames_of, spike["frame"])
        if i > 0:
            spike["before"] = f"-{spike['frame'] - events[i - 1][0]}f {events[i - 1][1]}"
        if i < len(events):
            spike["after"] = f"+{events[i][0] - spike['frame']}f {events[i][1]}"
    counts = {}
    for spike in spikes:
        counts[spike["kind"]] = counts.get(spike["kind"], 0) + 1
    return {"counts": counts, "worst": spikes[:limit]}


# --- tables -----------------------------------------------------------------------------------------------------------

def fmt(value):
    return "-" if value is None else (f"{value:g}" if isinstance(value, float) else str(value))


def present_cell(r):
    e = r.get("present_exit")
    if not e:
        return "-"
    if not e.get("n"):
        return f"none presented ({e.get('dropped', '?')} dropped)" if "dropped" in e else "none presented"
    return f"{e['p50']:g}/{e['p99']:g}/{e['max']:g} ({e['presented']} shown, {e['dropped']} dropped) {e['mode']}"


def ticks_cell(r):
    """Gameplay-window present ticks (the exit line's histogram also covers boot and menus)."""
    pw = ((r.get("window") or {}).get("present") or {})
    t = pw.get("ticks_1_2_3_4plus")
    return "/".join(str(x) for x in t) + f" @{pw['display_hz']:g}Hz, off {pw['off_cadence']}, >20.8 {pw['over_20.8']}" \
        if t else "-"


def run_table(results):
    lines = ["| run | outcome | frames | r2r p50/p99/max | r2r >17.5/>20.8/>33.3 | busy p50/p95/p99 | interval p50/p99/max "
             "| interval >16.7/>20.8/>33.3 | late phase (share; basis) | game_cpu / draw_done / gx_submit / frame_slot / "
             "pipeline (ms per late span) | acquire ms | present p50/p99/max | refresh ticks 1/2/3/4+ | slow compiles | "
             "skipped draws | load max | wall s | flags |",
             "|---|---|---:|---|---|---|---|---|---|---|---:|---|---|---:|---:|---:|---:|---|"]
    for r in results:
        w = r.get("window") or {}
        q = w.get("r2r") or {}
        b = w.get("busy") or {}
        late = w.get("late") or {}
        m = late.get("mean_ms") or {}
        phase = f"{late.get('dominant', '-')} ({late.get('dominant_share', '-')}; {late.get('basis', '-')})" \
            if late.get("n") else "-"
        parts = " / ".join(fmt(m.get(k)) for k in ("game_cpu", "draw_done", "gx_submit", "frame_slot",
                                                     "pipeline_wait")) if m else "-"
        lines.append(
            f"| {r['label']}/{r['stage']}-s{r['scenario']}-r{r['repeat']} | {r['outcome']} | {fmt(w.get('frames'))} | "
            f"{fmt(q.get('p50'))}/{fmt(q.get('p99'))}/{fmt(q.get('max'))} | "
            f"{fmt(q.get('over_17.5'))}/{fmt(q.get('over_20.8'))}/{fmt(q.get('over_33.3'))} | "
            f"{fmt(b.get('p50'))}/{fmt(b.get('p95'))}/{fmt(b.get('p99'))} | "
            f"{fmt(w.get('p50'))}/{fmt(w.get('p99'))}/{fmt(w.get('max'))} | "
            f"{fmt(w.get('over_16.7'))}/{fmt(w.get('over_20.8'))}/{fmt(w.get('over_33.3'))} | {phase} | {parts} | "
            f"{fmt(w.get('drawable_acquire_mean'))} | {present_cell(r)} | {ticks_cell(r)} | "
            f"{fmt(r['shaders'].get('slow_compiles'))} | {fmt(r.get('skipped_draws'))} | "
            f"{fmt((r.get('loading') or {}).get('max'))} | {fmt(r.get('wall_seconds'))} | "
            f"{'; '.join(r.get('contaminated') or []) or 'ok'} |")
    return "\n".join(lines)


# Comparison metrics: (name, getter). Late verdicts key off retrace-to-retrace and present intervals.
def _get(*path):
    def get(r):
        value = r
        for key in path:
            value = (value or {}).get(key) if isinstance(value, dict) else None
        return value
    return get


METRICS = {
    "r2r_p99": _get("window", "r2r", "p99"), "r2r_late": _get("window", "r2r", "over_20.8"),
    "r2r_max": _get("window", "r2r", "max"), "busy_p99": _get("window", "busy", "p99"),
    "interval_p99": _get("window", "p99"), "present_p99": _get("window", "present", "p99"),
    "present_late": _get("window", "present", "over_20.8"), "present_offcad": _get("window", "present", "off_cadence"),
}


def by_stage(results):
    groups = {}
    for r in results:
        if r.get("window"):
            groups.setdefault((r["stage"], r["scenario"]), []).append(r)
    return groups


def values_of(runs, metric):
    return [v for v in (METRICS[metric](r) for r in runs) if v is not None]


def noise(results):
    """Per metric: median over stages of the spread (max - min) between repeats of the same stage."""
    spreads = {m: [] for m in METRICS}
    for runs in by_stage(results).values():
        for m in METRICS:
            values = values_of(runs, m)
            if len(values) >= 2:
                spreads[m].append(max(values) - min(values))
    return {m: (round(statistics.median(v), 2) if v else None) for m, v in spreads.items()}


# Smallest delta a verdict may rest on: with zero repeat spread, 2x noise alone would call 0.01 ms "better".
RESOLUTION = {"r2r_p99": 0.05, "r2r_late": 2, "busy_p99": 0.1, "present_late": 3, "present_offcad": 2}


def compare_table(a_results, b_results, a_name="A", b_name="B"):
    """Per stage: median of repeats for A and B, delta, and whether it exceeds 2x the pooled repeat spread."""
    na, nb = noise(a_results), noise(b_results)
    pooled = {m: max([x for x in (na.get(m), nb.get(m)) if x is not None], default=None) for m in METRICS}
    ga, gb = by_stage(a_results), by_stage(b_results)
    shown = ("r2r_p99", "r2r_late", "busy_p99", "present_late", "present_offcad")
    head = " | ".join(f"{m} {a_name} | {m} {b_name} | d" for m in shown)
    lines = [f"| stage | n | {head} | verdict |", "|---|---:|" + "---:|---:|---:|" * len(shown) + "---|"]
    for key in sorted(set(ga) & set(gb)):
        ra, rb = ga[key], gb[key]
        cells, verdict = [], []
        cold_runs = [r for r in ra + rb if r.get("not_warm")]
        if cold_runs:
            verdict.append(f"NOT WARM ({len(cold_runs)} run(s)): no verdict")
        for m in shown:
            va, vb = values_of(ra, m), values_of(rb, m)
            if not va or not vb:
                cells.append("- | - | -")
                continue
            xa, xb = statistics.median(va), statistics.median(vb)
            d = xb - xa
            cells.append(f"{xa:g} | {xb:g} | {d:+.2f}")
            if m in ("r2r_p99", "r2r_late", "busy_p99", "present_late", "present_offcad") and not cold_runs:
                if abs(d) < RESOLUTION[m]:
                    continue  # below what the metric resolves (percentile bins, single frames)
                n = pooled.get(m)
                if n is None:
                    verdict.append(f"{m} ? (no repeats)") if d else None
                elif abs(d) > 2 * n and d:
                    verdict.append(f"{m} {'better' if d < 0 else 'WORSE'}")
        lines.append(f"| {key[0]}-s{key[1]} | {len(ra)}/{len(rb)} | {' | '.join(cells)} | "
                     f"{', '.join(verdict) or 'within noise'} |")
    lines.append("")
    lines.append("Gameplay-window present intervals (refresh ticks 1/2/3/4+, off-cadence, >20.8 ms), per run:")
    for name, runs in ((a_name, a_results), (b_name, b_results)):
        for r in runs:
            pw = ((r.get("window") or {}).get("present") or {})
            if pw.get("ticks_1_2_3_4plus"):
                lines.append(f"- {name} {r['stage']}-s{r['scenario']}-r{r['repeat']}: {'/'.join(map(str, pw['ticks_1_2_3_4plus']))}"
                             f" @{pw['display_hz']:g} Hz, off-cadence {pw['off_cadence']}, >20.8 {pw['over_20.8']}, "
                             f"states {pw.get('states')}")
    lines.append("")
    lines.append(f"Noise (median over stages of the spread between repeats of one stage): {a_name} {na}; {b_name} {nb}. "
                 "A per-stage delta counts only beyond 2x the larger spread; with one repeat there is no noise estimate.")
    return "\n".join(lines)


def write_summary(out, meta, results):
    sections = [f"# perf_bench {out.name}", "",
                f"{meta.get('started')} - {datetime.datetime.now().isoformat(timespec='seconds')}; mode "
                f"{meta.get('mode')}; set {meta.get('set')}; repeats {meta.get('repeat')}; lock {meta.get('lock_class')}",
                "", "Game pacing = retrace-to-retrace (r2r: consecutive frame-loop retrace marks, as frame-cost's "
                "retrace_pace.py); headroom = busy (interval - retrace wait); interval = seam-to-seam game-thread cadence "
                "(jitters with per-frame work; kept for reference). Display = on-screen present intervals (Petari present "
                "intervals exit line). Late = r2r > 20.8 ms; its phase is the game-thread part with the largest total over "
                "late spans (retrace wait excluded). Window: stage ready -> end of the stage driver script.", ""]
    for label, info in meta.get("apps", {}).items():
        sections.append(f"- {label}: {info['source']} sha256 {info['sha256'][:16]}")
    sections += ["", run_table(results), ""]
    for r in results:
        sp = r.get("spikes")
        if not sp:
            continue
        w = r.get("window") or {}
        sections += [f"### Spikes: {r['label']}/{r.get('drive')}-{r['stage']}-s{r['scenario']}-r{r['repeat']} "
                     f"({r['outcome']}; {sp['counts']}; phases {w.get('phase_frames')}; {r.get('events')} driver events)", "",
                     "| frame | kind | ms | phase | cause (ms) | event before | event after |", "|---:|---|---:|---|---|---|---|"]
        for s_ in sp["worst"]:
            sections.append(f"| {s_['frame']} | {s_['kind']} | {s_['ms']} | {s_['phase']} | {s_['cause']} | "
                            f"{s_.get('before', '-').replace('|', '/')} | {s_.get('after', '-').replace('|', '/')} |")
        sections.append("")
    labels = sorted({r["label"] for r in results})
    if len(labels) == 2:
        a = [r for r in results if r["label"] == labels[0]]
        b = [r for r in results if r["label"] == labels[1]]
        sections += ["## A/B", "", compare_table(a, b, labels[0], labels[1]), ""]
    else:
        sections += [f"Noise: {noise(results)}", ""]
    (out / "summary.md").write_text("\n".join(sections) + "\n")
    (out / "results.json").write_text(json.dumps({"meta": meta, "results": results}, indent=2, ensure_ascii=False) + "\n")
    return "\n".join(sections)


# --- commands ---------------------------------------------------------------------------------------------------------

def resolve_stages(args):
    if args.stages:
        chosen = []
        for item in args.stages.split(","):
            stage, _, scenario = item.partition(":")
            chosen.append((stage, int(scenario or 1)))
        return chosen
    if args.set == "quick":
        return list(QUICK)
    if args.set == "full":
        rows = sweep.enumerate_stages(args.disc / "files")
        return [(s["stage"], c["scenario"]) for s in rows if s["stage"] not in EXCLUDE_FULL for c in s["scenarios"]]
    raise SystemExit(f"perf_bench: unknown set {args.set}")


def start_bench(args, mode_label):
    out = args.out_root / args.name
    out.mkdir(parents=True, exist_ok=True)
    if free_gib(out) < 42:
        raise SystemExit(f"perf_bench: only {free_gib(out):.0f} GiB free (floor 40)")
    meta = {"started": datetime.datetime.now().isoformat(timespec="seconds"), "mode": mode_label,
            "set": args.stages or args.set, "repeat": args.repeat, "lock_class": args.lock_class, "apps": {}}
    return out, meta


def finish(args, out, meta, results, apps):
    prune_apps(args.warm_root)
    if not args.keep_metal and apps:
        with open(Path(args.warm_root) / "metal.lock", "w") as metal_lock:
            fcntl.flock(metal_lock, fcntl.LOCK_EX)
            for app in apps:
                shutil.rmtree(app.metal_dir, ignore_errors=True)
    # The bench dir can hold several invocations (labels, extra repeats): summarize every run in it.
    collected = []
    for path in sorted((out / "runs").glob("*/*/result.json")):
        try:
            collected.append(json.loads(path.read_text()))
        except (OSError, ValueError):
            pass
    previous = {}
    try:
        previous = json.loads((out / "results.json").read_text()).get("meta", {})
    except (OSError, ValueError):
        pass
    meta["apps"] = {**previous.get("apps", {}), **meta["apps"]}
    meta["invocations"] = previous.get("invocations", []) + [{"argv": sys.argv[1:], "seconds": meta.get("bench_seconds")}]
    text = write_summary(out, meta, collected or results)
    print(text)
    log(f"summary: {out / 'summary.md'}")


def batch(args, runs):
    """Run this whole bench inside ONE app lock (default): the visible, focus-taking windows come as one session
    instead of being scattered between other workers' runs. Returns True if this process should just exit."""
    if args.no_batch or os.environ.get("PERF_BENCH_IN_LOCK"):
        return False
    lock = MAIN / "build/locked-app.sh"
    if not lock.is_file():
        return False
    fill = 0 if args.cold else 1800  # (a fill, if needed, runs inside this session)
    bound = runs * (args.timeout + 30) + fill + 300
    minutes = round((runs * 75 + fill) / 60)
    print(f"[perf-bench] FOREGROUND SESSION: {runs} visible Petari runs that take focus, about {minutes} min "
          f"(bounded {bound // 60} min), {args.lock_class} lock owner {args.owner}; keyboard/gamepad input reaches the "
          "game and marks runs ASSISTED", flush=True)
    env = dict(os.environ, PERF_BENCH_IN_LOCK="1", PETARI_LOCK_CLASS=args.lock_class, PETARI_LOCK_TIMEOUT=str(bound))
    status = subprocess.run([str(lock), args.owner, sys.executable, "-I", str(Path(__file__).resolve())] + sys.argv[1:],
                            cwd=REPO, env=env).returncode
    raise SystemExit(status)


def cmd_run(args):
    batch(args, len(resolve_stages(args)) * args.repeat)
    out, meta = start_bench(args, "cold" if args.cold else "warm")
    app = BenchApp(args.app, args.warm_root, args.warm_key, cold=args.cold).freeze()
    if not args.cold:
        ensure_warm(args, app, fill_env=args.env if args.warm_key else ())
    meta["apps"][args.label] = {"source": str(app.source), "sha256": app.sha}
    chosen = resolve_stages(args)
    results = []
    t0 = time.time()
    try:
        for repeat in range(1, args.repeat + 1):
            for stage, scenario in chosen:
                results.append(run_once(args, out, args.label, app, stage, scenario, repeat, args.cold))
    finally:
        meta["bench_seconds"] = round(time.time() - t0)
        finish(args, out, meta, results, [app])


def cmd_ab(args):
    """Two variants, interleaved per stage (who goes first alternates). A variant is an app plus extra env, so one
    build can be compared against itself with a flag (e.g. --b-env PETARI_PRESENT_MODE=fifo)."""
    batch(args, 2 * len(resolve_stages(args)) * args.repeat)
    out, meta = start_bench(args, ("cold" if args.cold else "warm") + " A/B")
    a_label, b_label = args.a_label, args.b_label
    if a_label == b_label:
        raise SystemExit("perf_bench: --a-label and --b-label must differ")
    variants = {a_label: (BenchApp(args.a, args.warm_root, args.a_warm_key, cold=args.cold).freeze(), args.a_env),
                b_label: (BenchApp(args.b or args.a, args.warm_root, args.b_warm_key, cold=args.cold).freeze(), args.b_env)}
    loads = {a_label: args.a_load, b_label: args.b_load}
    if variants[a_label][0].sha == variants[b_label][0].sha and args.a_env == args.b_env and args.a_load == args.b_load:
        log("A and B are the same build and environment: this measures noise only")
    if not args.cold:
        # One team cache for both unless a side has its own --*-warm-key (filled with that side's app).
        for label, (app, env) in variants.items():
            # A keyed cache is filled with that side's environment (e.g. its PETARI_PIPELINE_SEED_DIR).
            ensure_warm(args, app, fill_env=env if app.warm_key else ())
    for label, (app, env) in variants.items():
        meta["apps"][label] = {"source": str(app.source), "sha256": app.sha, "env": env, "load": loads[label],
                               "warm_cache": app.warm_cache.name}
    chosen = resolve_stages(args)
    results = []
    t0 = time.time()
    try:
        for repeat in range(1, args.repeat + 1):
            for index, (stage, scenario) in enumerate(chosen):
                order = (a_label, b_label) if (repeat + index) % 2 else (b_label, a_label)
                for label in order:
                    app, env = variants[label]
                    results.append(run_once(args, out, label, app, stage, scenario, repeat, args.cold, env, loads[label]))
    finally:
        meta["bench_seconds"] = round(time.time() - t0)
        finish(args, out, meta, results, [variants[a_label][0]])


def cmd_warm(args):
    app = BenchApp(args.app, args.warm_root, args.warm_key)
    if args.print_path:
        print(app.warm_cache if warm_ok(app.warm_cache, app.bundle_id) else "")
        return
    if args.refresh:
        refuse_if_busy(args, "refresh the warm cache")
    ensure_warm(args, app, refresh=args.refresh, fill_env=args.fill_env)
    print(app.warm_cache)


def reanalyze(out):
    """Re-parse every run's app.log and frames.csv with the current analysis (run metadata is kept)."""
    for path in sorted((out / "runs").glob("*/*/result.json")):
        old = json.loads(path.read_text())
        run_dir = path.parent
        fresh = analyze_run(run_dir, run_dir / "user", old["stage"], old.get("exit_status"), old.get("timed_out", False),
                            old.get("drive"))
        old.update(fresh)
        path.write_text(json.dumps(old, indent=2, ensure_ascii=False) + "\n")
    data = json.loads((out / "results.json").read_text())
    data["results"] = [json.loads(p.read_text()) for p in sorted((out / "runs").glob("*/*/result.json"))]
    (out / "results.json").write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")


def cmd_report(args):
    if args.reanalyze:
        reanalyze(args.out_root / args.name)
        if args.vs:
            reanalyze(args.out_root / args.vs)
    data = json.loads((args.out_root / args.name / "results.json").read_text())
    if args.vs:
        other = json.loads((args.out_root / args.vs / "results.json").read_text())
        print(compare_table(data["results"], other["results"], args.name, args.vs))
    else:
        print(write_summary(args.out_root / args.name, data["meta"], data["results"]))


def cmd_sets(args):
    print("quick:", ", ".join(f"{s}:{n}" for s, n in QUICK))
    full = resolve_stages(argparse.Namespace(stages=None, set="full", disc=args.disc))
    print(f"full: {len(full)} stage/scenario runs (every scenario the stage sweep enumerates, minus "
          f"{', '.join(sorted(EXCLUDE_FULL))})")


def pid_alive(pid):
    try:
        os.kill(int(pid), 0)
        return True
    except (ValueError, ProcessLookupError):
        return False
    except PermissionError:
        return True


def busy_reasons(lock_root, warm_root, listing=None, own_lock=False):
    """Why destroying shared bench caches now could break someone's run: app-lock or sweep-lane holders and live
    tickets (unless this process runs inside its own batched lock), a held metal.lock (a bench app is using the bench
    Metal dir), or a running bench app (an executable under <warm-root>/apps/). Empty: safe."""
    reasons = []
    lock_root, warm_root = Path(lock_root), Path(warm_root)
    if not own_lock:
        for d in sorted(lock_root.glob(".petari-app.lock*")) + [lock_root / ".petari-sweep-lane.lock"]:
            if d.is_dir() and not d.name.endswith((".queue",)) and (d / "pid").is_file() and pid_alive((d / "pid").read_text().strip()):
                owner = (d / "owner").read_text().strip() if (d / "owner").is_file() else "?"
                reasons.append(f"{d.name} held by {owner}")
        for queue in (lock_root / ".petari-app.lock.queue", lock_root / ".petari-sweep-lane.lock.queue"):
            for ticket in sorted(queue.glob("*")) if queue.is_dir() else []:
                if pid_alive(ticket.name.rsplit("-", 1)[-1]):
                    reasons.append(f"waiting ticket {ticket.name} ({ticket.read_text().strip()})")
    metal_lock = warm_root / "metal.lock"
    if metal_lock.is_file():
        with open(metal_lock) as handle:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.flock(handle, fcntl.LOCK_UN)
            except BlockingIOError:
                reasons.append("metal.lock held (a bench app is using the bench Metal cache)")
    if listing is None:
        listing = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    apps = str(warm_root / "apps") + "/"
    for line in listing.splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[1].startswith(apps) and "Contents/MacOS/Petari" in parts[1].split(" ")[0]:
            reasons.append(f"bench app running (pid {parts[0]})")
    return reasons


def refuse_if_busy(args, what):
    if getattr(args, "force", False):
        return
    reasons = busy_reasons(MAIN / "build", args.warm_root, own_lock=bool(os.environ.get("PERF_BENCH_IN_LOCK")))
    if reasons:
        raise SystemExit(f"perf_bench: refusing to {what} while the shared bench caches may be in use: "
                         f"{'; '.join(reasons)} (--force overrides)")


def cmd_clean(args):
    if args.apps:
        # Only app clones: the newest --keep, recently used and running ones stay; no lock guard needed.
        before = free_gib(args.warm_root)
        prune_apps(args.warm_root, keep=args.keep)
        log(f"free disk {before:.1f} -> {free_gib(args.warm_root):.1f} GiB")
        return
    refuse_if_busy(args, "clean")
    base = darwin_cache_dir()
    with open(Path(args.warm_root) / "metal.lock", "a") if Path(args.warm_root).is_dir() else open(os.devnull) as lock:
        if Path(args.warm_root).is_dir():
            fcntl.flock(lock, fcntl.LOCK_EX)  # never under a running bench app
        for d in [base / BENCH_ID] + list(base.glob(BENCH_ID + "-*")):
            if d.exists():
                shutil.rmtree(d, ignore_errors=True)
                log(f"removed {d}")
    if args.all_warm:
        shutil.rmtree(args.warm_root, ignore_errors=True)
        log(f"removed {args.warm_root}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    default_app = MAIN / "build/macos-gx/native/app/Petari.app"

    def common(p):
        p.add_argument("--disc", type=Path, default=MAIN / "build/game-data/RMGE01")
        p.add_argument("--out-root", type=Path, default=REPO / "build/perf-bench")
        p.add_argument("--warm-root", type=Path, default=MAIN / "build/perf-bench-warm",
                       help="warm caches and bench app clones, shared by worktrees (main checkout build/, local)")
        p.add_argument("--source", type=Path, default=MAIN / "build/observatory-user-2", help="fixture save source")
        p.add_argument("--owner", default=OWNER, help="locked-app.sh owner name")

    def bench(p):
        common(p)
        p.add_argument("--name", default=None, help="output: <out-root>/<name> (default: set + mode + time)")
        p.add_argument("--set", choices=("quick", "full"), default="quick")
        p.add_argument("--stages", help="Stage:scenario list, e.g. HeavenlyBeachGalaxy:1,AstroGalaxy:2 (overrides --set)")
        p.add_argument("--repeat", type=int, default=1, help="passes over the stage list")
        p.add_argument("--cold", action="store_true", help="empty Metal/pipeline/Dawn caches for every run")
        p.add_argument("--lock-class", choices=("quiet", "functional"), default="quiet",
                       help="functional runs are marked contaminated (not timing evidence)")
        p.add_argument("--timeout", type=int, default=None, help="seconds per app run, inside the lock (420; drives 1500)")
        p.add_argument("--lock-wait", type=int, default=7200, help="extra seconds allowed for waiting on the lock")
        p.add_argument("--frames", type=int, default=None, help="PETARI_SMOKE_FRAMES (safety bound; 12000, drives 60000)")
        p.add_argument("--drive", choices=("goodegg1", "soak", "replay", "stage"), default=None,
                       help="a gameplay driver instead of the stage script: goodegg1 (Good Egg mission 1 to the star, "
                            "save, observatory), soak (one cycle: stage, back to the observatory, End Game save, title), "
                            "replay (--route, a recorded route; FAILs where no star is collected, frames stay valid), "
                            "stage (the stage script analyzed as a drive: whole run + spikes; with --env "
                            "PETARI_STAGE_WARP=x,y,z onto a placed PowerStar and PETARI_SMOKE_QUIT_DELAY=<frames> it "
                            "records a genuine star-get and the return to the observatory)")
        p.add_argument("--route", type=Path, default=None, help="route CSV for --drive replay (recording_segments.py)")
        p.add_argument("--idle-frames", type=int, default=600, help="PETARI_STAGE_IDLE_FRAMES")
        p.add_argument("--env", action="append", default=[], metavar="KEY=VALUE", help="extra app environment")
        p.add_argument("--rerun", action="store_true", help="rerun runs that already have a result")
        p.add_argument("--keep-user", action="store_true", help="keep the run's cache databases")
        p.add_argument("--keep-metal", action="store_true", help="keep the bench Metal dir after the bench")
        p.add_argument("--no-batch", action="store_true",
                       help="take the app lock per run instead of once for the whole bench (focus steals get scattered)")
        p.add_argument("--visit", default="", help="if the warm cache must be filled first: extra warm-up visits (see warm)")

    p = sub.add_parser("run", help="one app over a stage set")
    bench(p)
    p.add_argument("--warm-key", default=None, help="use (fill if missing) the warm cache keyed <name>, filled with --app")
    p.add_argument("--app", type=Path, default=default_app)
    p.add_argument("--label", default="main", help="run label (directory under runs/)")
    p.set_defaults(fn=cmd_run)
    p = sub.add_parser("ab", help="two apps, interleaved per stage")
    bench(p)
    p.add_argument("--a", type=Path, default=default_app, help="baseline app (default: main checkout build)")
    p.add_argument("--b", type=Path, default=None,
                   help="candidate app, e.g. ~/macio-worktrees/<w>/build/macos-gx/native/app/Petari.app (default: --a)")
    p.add_argument("--a-env", action="append", default=[], metavar="KEY=VALUE", help="extra environment for A only")
    p.add_argument("--b-env", action="append", default=[], metavar="KEY=VALUE", help="extra environment for B only")
    p.add_argument("--a-load", default=None, metavar="SHELL_CMD",
                   help="a concurrent load started just before each A app run and killed after it (process group)")
    p.add_argument("--b-load", default=None, metavar="SHELL_CMD", help="the same for B")
    p.add_argument("--a-warm-key", default=None, help="A's own warm cache (filled with A's app); default the team cache")
    p.add_argument("--b-warm-key", default=None, help="B's own warm cache (filled with B's app); default the team cache")
    p.add_argument("--a-label", default="A")
    p.add_argument("--b-label", default="B")
    p.set_defaults(fn=cmd_ab)
    p = sub.add_parser("warm", help="fill (or locate) the team warm cache")
    common(p)
    p.add_argument("--app", type=Path, default=default_app, help="the app that fills it (default: main build)")
    p.add_argument("--refresh", action="store_true", help="rebuild it even if it exists")
    p.add_argument("--warm-key", default=None, help="a separately keyed cache (cache-<key>-<name>), filled with --app")
    p.add_argument("--fill-env", action="append", default=[], metavar="KEY=VALUE",
                   help="environment for the fill runs (e.g. PETARI_PIPELINE_SEED_DIR=<seeds> for a keyed cache)")
    p.add_argument("--force", action="store_true", help="refresh even while bench runs or app-lock holders exist")
    p.add_argument("--print-path", action="store_true", help="print the current warm cache path ('' if none)")
    p.add_argument("--visit", default="", help="extra warm-up stage visits after the global fill: quick, full or Stage:n,...")
    p.add_argument("--frames", type=int, default=12000)
    p.add_argument("--lock-wait", type=int, default=7200)
    p.set_defaults(fn=cmd_warm)
    p = sub.add_parser("harvest", help="merge benches' harvested unseeded configs (and build a test seed dir)")
    p.add_argument("out")
    p.add_argument("benches", nargs="+", help="bench dirs (e.g. build/perf-bench/routes-warm)")
    p.add_argument("--seed-dir", type=Path, default=None, help="seeds to extend into OUT/seeds (e.g. the app's Resources)")
    p.set_defaults(fn=cmd_harvest)
    p = sub.add_parser("report", help="re-print a bench summary, or compare two benches")
    common(p)
    p.add_argument("name")
    p.add_argument("--vs")
    p.add_argument("--reanalyze", action="store_true", help="re-parse the runs' logs and CSVs with the current analysis")
    p.set_defaults(fn=cmd_report)
    p = sub.add_parser("sets", help="print the stage sets")
    common(p)
    p.set_defaults(fn=cmd_sets)
    p = sub.add_parser("clean", help="remove bench Metal dirs (and with --all-warm every warm cache)")
    common(p)
    p.add_argument("--all-warm", action="store_true")
    p.add_argument("--force", action="store_true", help="clean even while bench runs or app-lock holders exist")
    p.add_argument("--apps", action="store_true", help="only prune app clones (keep the newest --keep, recent, running)")
    p.add_argument("--keep", type=int, default=3)
    p.set_defaults(fn=cmd_clean)
    p = sub.add_parser("_launch", help=argparse.SUPPRESS)
    p.add_argument("--spec", required=True)
    p.set_defaults(fn=cmd_launch)
    args = parser.parse_args()
    if getattr(args, "drive", None) == "replay" and not args.route:
        parser.error("--drive replay needs --route")
    if hasattr(args, "drive"):
        args.frames = args.frames or (60000 if args.drive else 12000)
        args.timeout = args.timeout or (1500 if args.drive else 420)
    if hasattr(args, "visit"):
        args.visit = resolve_stages(argparse.Namespace(stages=None if args.visit in ("quick", "full") else args.visit,
                                                       set=args.visit, disc=args.disc)) if args.visit else []
    if getattr(args, "name", "") is None:
        stamp = datetime.datetime.now().strftime("%m%d-%H%M")
        args.name = f"{args.stages and 'custom' or args.set}-{'cold' if args.cold else 'warm'}-{stamp}"
    args.fn(args)


if __name__ == "__main__":
    main()
