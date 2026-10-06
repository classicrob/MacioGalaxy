#!/usr/bin/env python3
"""Exercise live gravity editing through an isolated background test server."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

TOOLS = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--app", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--owner", default="gravity")
    p.add_argument("--capture-wide", action="store_true", help="Capture unedited starter and non-spherical planets")
    args = p.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    trigger = output / "begin-gravity-test"
    trigger.unlink(missing_ok=True)
    directory = output / "server"
    env = dict(os.environ, PETARI_EDITOR="1", PETARI_MODS="LevelEditor",
               PETARI_EDITOR_GRAVITY_TEST=str(trigger),
               PETARI_EDITOR_GRAVITY_SHOT=str(output / "gravity.png"),
               PETARI_SURFACE_DUMP=str(output / "surfaces"),
               PETARI_SURFACE_DUMP_LABELS="gravity-edited,gravity-dead", PETARI_SURFACE_DUMP_SPAN="15",
               PETARI_SURFACE_DUMP_EVERY="5")
    if args.capture_wide:
        env.update(PETARI_EDITOR_GRAVITY_CAPTURE="1", PETARI_SURFACE_DUMP_LABELS="gravity-starter,gravity-nonspherical")
    server = [sys.executable, str(TOOLS / "test_server.py")]
    try:
        subprocess.run(server + ["start", "--app", str(args.app.resolve()), "--dir", str(directory),
                                "--owner", args.owner, "--warm-current-stage"], env=env, check=True)
        trigger.write_text("start\n")
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            log = (directory / "server.log").read_text(errors="replace")
            result = [line for line in log.splitlines() if "[editor-gravity]" in line]
            if any("FAIL:" in line for line in result):
                print("\n".join(result))
                return 1
            if any("PASS" in line for line in result):
                print("\n".join(result))
                return 0
            time.sleep(0.5)
        print("FAIL: timed out waiting for gravity smoke", file=sys.stderr)
        return 1
    finally:
        if (directory / "server.pid").exists():
            subprocess.run(server + ["stop", "--dir", str(directory)], check=False)
        trigger.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
