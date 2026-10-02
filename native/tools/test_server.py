#!/usr/bin/env python3
"""Persistent test server client: one background Petari stays up and takes commands (src/Game/Scene/TestServer.cpp).

  native/tools/test_server.py start --app <Petari> [--stage EggStarGalaxy --scenario 1] [--dir build/test-server/<name>]
  native/tools/test_server.py send  --dir <dir> status | warp <Stage> [<scenario>] | reload [start] | quit
  native/tools/test_server.py stop  --dir <dir>

`start` boots once (warm shader seed, PETARI_FAST_BOOT=1) through build/locked-app.sh and returns when the app answers `status`;
the app holds one app slot until `stop`/`quit`. `send` writes a command file and waits for its reply (exit 1 on "error").
Files: <dir>/server.log (the app's stderr), <dir>/cmd, <dir>/reply, <dir>/server.pid.
"""
import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))


def main_checkout():
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=TOOLS,
                            capture_output=True, text=True, check=True).stdout.strip()
    return Path(common).parent


def send(directory, command, timeout):
    name = f"{time.time_ns()}"
    cmd = Path(directory) / "cmd"
    tmp = cmd / f"{name}.tmp"
    tmp.write_text(command + "\n")
    tmp.rename(cmd / f"{name}.txt")
    reply = Path(directory) / "reply" / f"{name}.txt"
    deadline = time.time() + timeout
    while time.time() < deadline:
        if reply.is_file():
            return reply.read_text().strip()
        time.sleep(0.05)
    return "error no reply within %ds" % timeout


def cmd_start(args):
    import stage_sweep as sweep
    import warm_seed
    main = main_checkout()
    directory = Path(args.dir or main / "build/test-server" / f"{os.getpid()}").resolve()
    if (directory / "server.pid").exists():
        sys.exit(f"test-server: {directory} already has a server (stop it first)")
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "cmd").mkdir(exist_ok=True)
    (directory / "reply").mkdir(exist_ok=True)
    user = directory / "user"
    if user.exists():
        subprocess.run(["rm", "-rf", str(user)], check=True)
    sweep.load_fixture_module().create(main / "build/observatory-user-2", user, "stage")
    app = Path(args.app).resolve()

    class A:
        pass
    a = A()
    a.app, a.disc, a.frames, a.idle_frames, a.pipeline_policy = app, main / "build/game-data/RMGE01", 10_000_000, 10_000_000, "default"
    command, env_extra = sweep.app_command(a, user, args.stage, args.scenario)
    command += []
    wargs = argparse.Namespace(app=app, seed_root=str(main / "build/shader-seed"), stages=warm_seed.DEFAULT_STAGES, owner="test-server",
                               timeout=900, idle=900, force=False, user=user)
    wargs.sha = warm_seed.sha256_of(app)
    seed = warm_seed.ensure(wargs)
    warm_seed.prepare(wargs)
    env = dict(os.environ, **env_extra, PETARI_LOCK_CLASS="functional", PETARI_FAST_BOOT="1", PETARI_STAGE_GATE_MS="0",
               PETARI_SHADER_SEED_DIR=str(seed), PETARI_TEST_SERVER=str(directory))
    env.update(PETARI_SMOKE_FRAMES=str(10_000_000), PETARI_STAGE_IDLE_FRAMES=str(10_000_000))
    full = [str(main / "build/locked-app.sh"), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die", str(args.lifetime)] + command
    log = (directory / "server.log").open("wb")
    t0 = time.time()
    proc = subprocess.Popen(full, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    (directory / "server.pid").write_text(str(proc.pid))
    while time.time() - t0 < args.boot_timeout:
        if proc.poll() is not None:
            sys.exit(f"test-server: the app exited ({proc.returncode}); see {directory}/server.log")
        text = send(directory, "status", 5)
        if text.startswith("ok") and "not-playable" not in text:
            print(f"{directory}\nready in {time.time() - t0:.1f} s: {text}")
            return 0
        time.sleep(1)
    sys.exit("test-server: boot timeout")


def cmd_send(args):
    text = send(args.dir, " ".join(args.words), args.timeout)
    print(text)
    return 1 if text.startswith("error") else 0


def cmd_stop(args):
    pid_file = Path(args.dir) / "server.pid"
    send(args.dir, "quit", 5)
    time.sleep(1)
    if pid_file.is_file():
        try:
            os.killpg(int(pid_file.read_text()), signal.SIGTERM)
        except (ProcessLookupError, ValueError):
            pass
        pid_file.unlink(missing_ok=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("start")
    p.set_defaults(fn=cmd_start)
    p.add_argument("--app", required=True)
    p.add_argument("--stage", default="EggStarGalaxy")
    p.add_argument("--scenario", type=int, default=1)
    p.add_argument("--dir")
    p.add_argument("--owner", default="test-server")
    p.add_argument("--lifetime", type=int, default=7200, help="seconds the app may live (an alarm)")
    p.add_argument("--boot-timeout", type=int, default=900)
    p = sub.add_parser("send")
    p.set_defaults(fn=cmd_send)
    p.add_argument("--dir", required=True)
    p.add_argument("--timeout", type=int, default=180)
    p.add_argument("words", nargs="+")
    p = sub.add_parser("stop")
    p.set_defaults(fn=cmd_stop)
    p.add_argument("--dir", required=True)
    args = parser.parse_args()
    sys.exit(args.fn(args) or 0)


if __name__ == "__main__":
    main()
