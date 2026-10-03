#!/usr/bin/env python3
"""Example 1: a small course on Good Egg's starting planet (Disk Garden), mission 1.

From Mario's start: a trail of coins hugging the planet, a Sling Star that flings Mario straight up
through a column of coins, and a Power Star waiting at the top (missions 1, 4 and 6: layer A). Then the checks, and
a patch mod saved into the workspace's user folder.

    native/tools/petari_level/examples/sling_star_course.py [--ws build/ws/sling] [--disc build/game-data/RMGE01] [--save]
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from petari_level import Level  # noqa: E402

ROOT = Path(__file__).resolve().parents[4]


def add(a, b, k=1.0):
    return [a[i] + b[i] * k for i in range(3)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ws", default=str(ROOT / "build/ws/sling"))
    ap.add_argument("--disc", default=str(ROOT / "build/game-data/RMGE01"))
    ap.add_argument("--save", action="store_true", help="save the patch mod 'Sling Star Course'")
    args = ap.parse_args()

    lv = Level.init(args.ws, disc=args.disc, galaxy="EggStarGalaxy", mission=1, force=True)
    # Where the mission starts, and which way is up there (the planet's disk gravity).
    start = lv.get({"class": "Mario", "zone": "EggStarGalaxy", "mission": 1, "table": "start/layera"})["pos"]
    up = [-v for v in lv.gravity(start)["down"]]
    planet = lv.ground(start)["on"]
    center = lv.get(planet)["pos"]
    # Toward the planet's middle, flat on the ground plane.
    d = [center[i] - start[i] for i in range(3)]
    dot = sum(d[i] * up[i] for i in range(3))
    flat = [d[i] - up[i] * dot for i in range(3)]
    n = sum(v * v for v in flat) ** 0.5
    ahead = [v / n for v in flat]
    sling = add(start, ahead, 1000)

    with lv.batch(key="sling-course", label="Sling Star course") as b:
        b.array("Coin", "line", start=add(start, ahead, 250), end=add(start, ahead, 800), count=5,
                on_ground=True, height=80, label="trail")
        b.add("SpinDriver", sling, on_ground=True, height=120, args={"Speed / flight time": 40}, label="sling")
        b.array("Coin", "line", start=add(sling, up, 350), end=add(sling, up, 950), count=5, label="column")
        # No layer of the galaxy zone is active in mission 1 alone (the API lists them): layer A is missions 1, 4, 6.
        b.add("PowerStar", add(sling, up, 1150), missions=[1, 4, 6], args={"Star ID": 1}, up=up, label="star")
    result = b.result
    print(json.dumps({"created": len(result["created"]), "placements": result.get("placements", []),
                      "checks_introduced": result["checks_introduced"]}, indent=1))
    print(json.dumps(lv.checks(), indent=1))
    if args.save:
        print(json.dumps(lv.save("Sling Star Course"), indent=1))
    lv.close()


if __name__ == "__main__":
    main()
