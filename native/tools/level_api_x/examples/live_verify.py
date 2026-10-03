#!/usr/bin/env python3
"""Live verification of example 1 in a running game (AGENT_API_X.md "Live mode").

Builds the Sling Star course headless (sling_star_course.py's workspace), then in a test server started
with PETARI_EDITOR=1: loads the editor model, syncs the workspace's batch, reloads, compares the headless
ground with the engine's, screenshots the course, drops Mario onto the Sling Star and traces his height
along gravity to see whether the launch reaches the Power Star.

    PETARI_EDITOR=1 native/tools/test_server.py start --app <Petari> --dir build/test-server/me
    native/tools/level_api_x/examples/live_verify.py --server build/test-server/me [--shots build/shots/level-api-x]
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from level_api_x import Level, LevelError, Live  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = Path(__file__).resolve().parents[4]


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--server", required=True, help="the test server's directory")
    ap.add_argument("--ws", default=str(ROOT / "build/ws/sling"))
    ap.add_argument("--shots", default=str(ROOT / "build/shots/level-api-x"))
    args = ap.parse_args()

    subprocess.run([sys.executable, str(HERE / "sling_star_course.py"), "--ws", args.ws], check=True, stdout=subprocess.DEVNULL)
    lv = Level.open(args.ws)
    live = Live(args.server)
    report = {"begin": live.begin()}
    report["sync"] = live.sync(lv)
    report["reload"] = live.reload()

    sling = lv.get("@sling")
    star = lv.get("@star")
    up = sling["up"]
    # Headless vs engine: the ground under the Sling Star along gravity.
    probe = [sling["pos"][i] + up[i] * 400 for i in range(3)]
    headless = lv.ground(probe)
    engine = live.ground(probe)
    report["ground"] = {"headless": headless.get("point"), "engine": engine.get("point"), "engine_gravity": engine.get("gravity"),
                        "headless_down": headless.get("down")}
    report["checks_live"] = live.checks()

    # Mario on the course start, then onto the Sling Star (it catches him within 240 units).
    start = lv.get("@trail.0")["pos"]
    report["play"] = live.play(pos=start, up=up)
    live.wait(30)
    Path(args.shots).mkdir(parents=True, exist_ok=True)
    report["shot_course"] = live.screenshot(Path(args.shots) / "course.png")
    # Onto the Sling Star (it catches Mario within 240 units and flings him along its up).
    onto = [sling["pos"][i] + up[i] * 60 for i in range(3)]
    try:
        report["warp_sling"] = live.warp(onto)
    except LevelError:  # an app without the warp command: Play from a marker there
        report["warp_sling"] = live.play(pos=onto, up=up)
    base = dot(sling["pos"], up)
    heights = []
    for _ in range(40):
        m = live.wait(6)["mario"]
        if m.get("present"):
            heights.append(round(dot(m["pos"], up) - base))
    report["heights_above_sling"] = heights
    report["star_height_above_sling"] = round(dot(star["pos"], up) - base)
    report["reached_star_height"] = bool(heights) and max(heights) >= report["star_height_above_sling"] - 150
    report["shot_end"] = live.screenshot(Path(args.shots) / "after-launch.png")
    print(json.dumps(report, indent=1))
    lv.close()


if __name__ == "__main__":
    main()
