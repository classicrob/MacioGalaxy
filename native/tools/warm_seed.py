#!/usr/bin/env python3
"""Warm shader seed for live runs: no more cold pipeline compiles at every launch.

A fresh --user dir has no dawn_cache.db / pipeline_cache.db and the build has no Metal cache, so each run recompiles
every pipeline (MTLCompilerService at 100% CPU, ~55 s startup). A warm seed is, per app build (sha256 of the binary),

    build/shader-seed/<sha16>/{seed.json, metal/dev.petari.Petari, dawn_cache.db, pipeline_cache.db}

The app already reads PETARI_SHADER_SEED_DIR (native/app/smoke_storage.cpp: the Metal files, APFS-cloned, only if
seed.json's exe_sha256 matches this binary) and snapshots the user dir's two databases into its private cache. This tool
adds what was missing: publishing a *warm* seed from a clean warm-up run, and cloning the databases into a run's user dir.
Every run still writes only to its own private clones.

For other workers, one line (publishes first if this build has no seed, then exports PETARI_SHADER_SEED_DIR):

    eval $(native/tools/warm_seed.py env --app <Petari binary>)

and, for each run's --user directory (a copy-on-write clone, a few ms):

    native/tools/warm_seed.py prepare --app <Petari binary> --user <dir>

Commands: publish (explicit; --force redoes it), env, prepare, status. The seed is per build: after a rebuild `env` publishes
again by itself (a warm-up of the --stages below, ~10 min; one publisher at a time, the others wait for it).
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parents[1]
# The stages the editor workers use plus the observatory (AstroGalaxy): their shaders are what a live run needs.
DEFAULT_STAGES = ["EggStarGalaxy", "OceanRingGalaxy", "KoopaBattleVs1Galaxy", "AstroGalaxy"]
DBS = ("dawn_cache.db", "pipeline_cache.db")


def main_checkout():
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=REPO,
                            capture_output=True, text=True, check=True).stdout.strip()
    return Path(common).parent


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def seed_dir(args):
    return Path(args.seed_root) / args.sha[:16]


def seed_ok(args):
    root = seed_dir(args)
    try:
        recorded = json.loads((root / "seed.json").read_text())
    except (OSError, ValueError):
        return False
    return (recorded.get("exe_sha256") == args.sha and recorded.get("warm") is True
            and (root / "metal/dev.petari.Petari").is_dir() and all((root / db).is_file() for db in DBS))


def clone(source, destination):
    """APFS clone (copy-on-write; later writes to either side stay private), falling back to a copy."""
    if subprocess.run(["cp", "-c", str(source), str(destination)], capture_output=True).returncode != 0:
        shutil.copyfile(source, destination)


def clone_tree(source, destination):
    if subprocess.run(["cp", "-cR", str(source), str(destination)], capture_output=True).returncode != 0:
        shutil.copytree(source, destination)


def snapshot_db(source, destination):
    """A consistent copy of a SQLite db (WAL included) after the writer exited."""
    import sqlite3
    src = sqlite3.connect(f"file:{source}?mode=ro", uri=True)
    dst = sqlite3.connect(destination)
    with dst:
        src.backup(dst)
    src.close()
    dst.close()


def warm_up(args):
    """Runs one stage smoke per stage in a fresh private user dir, each starting from the seed so far, and publishes the
    union. Returns the published directory."""
    main = main_checkout()
    sys.path.insert(0, str(TOOLS))
    import stage_sweep as sweep
    work = Path(args.seed_root) / f"{args.sha[:16]}.warming-{os.getpid()}"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    current = None  # the seed dir so far: {seed.json, metal, dbs}
    log = lambda text: print(f"[warm-seed] {text}", file=sys.stderr, flush=True)
    for number, stage in enumerate(args.stages):
        user = work / f"user{number}"
        sweep.load_fixture_module().create(main / "build/observatory-user-2", user, "stage")
        if current is not None:
            for db in DBS:
                clone(current / db, user / db)

        class A:
            pass
        a = A()
        a.app, a.disc, a.frames, a.idle_frames, a.pipeline_policy = Path(args.app), main / "build/game-data/RMGE01", 60000, args.idle, "default"
        command, env_extra = sweep.app_command(a, user, stage, 1)
        env = dict(os.environ, **env_extra, PETARI_LOCK_CLASS="functional")
        if current is not None:
            env["PETARI_SHADER_SEED_DIR"] = str(current)
        full = [str(main / "build/locked-app.sh"), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.timeout)] + command
        t0 = time.time()
        with (work / f"warm{number}.log").open("wb") as stream:
            status = subprocess.run(full, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
        log(f"{stage}: exit {status}, {time.time() - t0:.0f} s")
        cache = user / "cache"
        metal = cache / "metal/dev.petari.Petari"
        if not metal.is_dir():
            raise SystemExit(f"warm-seed: {stage} left no Metal cache (see {work}/warm{number}.log)")
        nxt = work / f"seed{number}"
        (nxt / "metal").mkdir(parents=True)
        clone_tree(metal, nxt / "metal/dev.petari.Petari")
        for db in DBS:
            if (cache / db).is_file():
                snapshot_db(cache / db, nxt / db)
            elif current is not None:
                clone(current / db, nxt / db)
            else:
                raise SystemExit(f"warm-seed: {stage} wrote no {db}")
        (nxt / "seed.json").write_text(json.dumps({"exe_sha256": args.sha, "warm": True, "stages": args.stages[:number + 1],
                                                   "published": time.time()}, indent=2) + "\n")
        shutil.rmtree(user, ignore_errors=True)
        if current is not None:
            shutil.rmtree(current, ignore_errors=True)
        current = nxt
    final = seed_dir(args)
    shutil.rmtree(final, ignore_errors=True)
    current.rename(final)
    shutil.rmtree(work, ignore_errors=True)
    log(f"published {final}")
    return final


def ensure(args):
    """The seed dir for this build, publishing it first if missing. One publisher at a time (flock); the others wait."""
    import fcntl
    root = Path(args.seed_root)
    root.mkdir(parents=True, exist_ok=True)
    if seed_ok(args) and not args.force:
        return seed_dir(args)
    with open(root / f"{args.sha[:16]}.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if seed_ok(args) and not args.force:
            return seed_dir(args)
        print(f"[warm-seed] no warm seed for build {args.sha[:16]}: warming {', '.join(args.stages)}", file=sys.stderr, flush=True)
        return warm_up(args)


def prepare(args):
    root = ensure(args)
    user = Path(args.user)
    user.mkdir(parents=True, exist_ok=True)
    for db in DBS:
        if not (user / db).exists():
            clone(root / db, user / db)
    return root


def cmd_publish(args):
    print(ensure(args))


def cmd_env(args):
    print(f"export PETARI_SHADER_SEED_DIR={ensure(args)}")


def cmd_prepare(args):
    prepare(args)


def cmd_status(args):
    print(json.dumps({"app": str(args.app), "sha256": args.sha, "seed": str(seed_dir(args)), "ok": seed_ok(args)}, indent=1))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name, fn in (("publish", cmd_publish), ("env", cmd_env), ("prepare", cmd_prepare), ("status", cmd_status)):
        p = sub.add_parser(name)
        p.set_defaults(fn=fn)
        p.add_argument("--app", required=True, help="the Petari binary (Contents/MacOS/Petari)")
        p.add_argument("--seed-root", default=str(main_checkout() / "build/shader-seed"))
        p.add_argument("--stages", type=lambda v: [s for s in v.split(",") if s], default=DEFAULT_STAGES)
        p.add_argument("--owner", default="warm-seed", help="locked-app.sh owner name for the warm-up runs")
        p.add_argument("--timeout", type=int, default=900)
        p.add_argument("--idle", type=int, default=900, help="idle frames per warm-up stage")
        p.add_argument("--force", action="store_true", help="publish again even if a valid seed exists")
        if name == "prepare":
            p.add_argument("--user", required=True, help="the run's --user directory")
    args = parser.parse_args()
    args.sha = sha256_of(args.app)
    fn = args.fn
    fn(args)


if __name__ == "__main__":
    main()
