#!/usr/bin/env python3
"""Example: a coin-arc + Launch Star + Power Star course on Good Egg's starting planet (Disk Garden).

From Mario's mission-1 start: a short coin trail to a Launch Star; the Launch Star flies a new path
in a high arc over the planet's middle to its far side; an arc of coins traces the flight; a Power
Star waits where the flight comes down. The Launch Star and the star go in mission 1 only (Disk Garden's layer E): in the
common layer it would be the 17th Launch Star of mission 2, which the Checks report as an error
(the game keeps paths for 16), so the script shows that probe first as a dry run.

    native/tools/level_api_x/examples/launch_star_course.py [--ws build/ws/launch] [--disc ...] [--save]
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from level_api_x import Level  # noqa: E402

ROOT = Path(__file__).resolve().parents[4]


def add(a, b, k=1.0):
    return [a[i] + b[i] * k for i in range(3)]


def flat_unit(v, up):
    d = sum(v[i] * up[i] for i in range(3))
    f = [v[i] - up[i] * d for i in range(3)]
    n = sum(x * x for x in f) ** 0.5
    return [x / n for x in f]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ws", default=str(ROOT / "build/ws/launch"))
    ap.add_argument("--disc", default=str(ROOT / "build/game-data/RMGE01"))
    ap.add_argument("--save", action="store_true", help="save the patch mod 'Launch Star Course'")
    args = ap.parse_args()

    lv = Level.init(args.ws, disc=args.disc, galaxy="EggStarGalaxy", mission=1, force=True)
    start = lv.get({"class": "Mario", "zone": "EggStarGalaxy", "table": "start/layera"})["pos"]
    up = [-v for v in lv.gravity(start)["down"]]
    planet = lv.ground(start)["on"]                           # DiskGardenZone:common/objinfo:11
    zone = planet.split(":")[0]                               # a Launch Star and its path share a zone
    center = lv.get(planet)["pos"]
    ahead = flat_unit([center[i] - start[i] for i in range(3)], up)
    launch = add(start, ahead, 450)
    landing = lv.ground(add(add(start, ahead, 3000), up, 800))["point"]   # the far side of the disk
    apex = add([(launch[i] + landing[i]) / 2 for i in range(3)], up, 2600)

    def course(layer_ops):
        return [
            {"op": "array", "class": "Coin", "shape": "line", "from": add(start, ahead, 120), "to": add(start, ahead, 330),
             "count": 3, "on_ground": True, "height": 80, "as": "trail"},
            {"op": "path", "zone": zone, "points": [add(launch, up, 150), apex, add(landing, up, 450)], "as": "flight"},
            {"op": "add", "class": "Launch Star", "pos": launch, "on_ground": True, "height": 150, "path": "@flight",
             "zone": zone, "as": "launch", **layer_ops},
            {"op": "array", "class": "Coin", "shape": "arc", "from": add(launch, up, 500), "to": add(landing, up, 650),
             "peak": 2200, "count": 9, "down": [-v for v in up], "as": "arc"},
            {"op": "add", "class": "PowerStar", "pos": landing, "on_ground": True, "height": 180, "args": {"Star ID": 1},
             "missions": [1], "as": "star"},
        ]

    probe = lv.apply(course({}), dry_run=True)                # the Launch Star in every mission
    print(json.dumps({"probe_common_layer_checks": probe["checks_introduced"]}, indent=1))
    result = lv.apply(course({"missions": [1]}), key="launch-course", label="Launch Star course")
    print(json.dumps({"created": [(o["ref"], o.get("label"), o["class"]) for o in result["created"]],
                      "checks_introduced": result["checks_introduced"]}, indent=1))
    print(json.dumps(lv.checks(), indent=1))
    if args.save:
        print(json.dumps(lv.save("Launch Star Course"), indent=1))
    lv.close()


if __name__ == "__main__":
    main()
