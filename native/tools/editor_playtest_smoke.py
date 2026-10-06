#!/usr/bin/env python3
"""Exercise the editor play loop through an isolated background test server."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import time

TOOLS = Path(__file__).resolve().parent


def progress_files(user):
    paths = [user / "progress.json", *(user / "mods-saves").rglob("*")]
    return {str(path.relative_to(user)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in paths if path.is_file()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--app", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--owner", default="gravity")
    args = p.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    trigger = output / "begin-playtest-test"
    trigger.unlink(missing_ok=True)
    directory = output / "server"
    env = dict(os.environ, PETARI_EDITOR="1", PETARI_MODS="LevelEditor",
               PETARI_EDITOR_PLAYTEST_TEST=str(trigger),
               PETARI_SURFACE_DUMP=str(output / "surfaces"),
               PETARI_SURFACE_DUMP_LABELS="playtest-trail,playtest-cleared,playtest-missed", PETARI_SURFACE_DUMP_SPAN="15",
               PETARI_SURFACE_DUMP_EVERY="5")
    server = [sys.executable, str(TOOLS / "test_server.py")]
    try:
        subprocess.run(server + ["start", "--app", str(args.app.resolve()), "--dir", str(directory),
                                "--owner", args.owner, "--warm-current-stage"], env=env, check=True)
        before = progress_files(directory / "user")
        trigger.write_text("start\n")
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            log = (directory / "server.log").read_text(errors="replace")
            result = [line for line in log.splitlines() if "[editor-playtest]" in line]
            if any("FAIL:" in line for line in result):
                print("\n".join(result))
                return 1
            if any("[editor-playtest] PASS:" in line for line in result):
                print("\n".join(result))
                if progress_files(directory / "user") != before:
                    print("FAIL: personal progress or mod sidecar files changed during play-test", file=sys.stderr)
                    return 1
                print("PASS: personal progress and mod sidecar files unchanged")
                return 0
            time.sleep(0.5)
        print("FAIL: timed out waiting for playtest smoke", file=sys.stderr)
        return 1
    finally:
        if (directory / "server.pid").exists():
            subprocess.run(server + ["stop", "--dir", str(directory)], check=False)
        trigger.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
