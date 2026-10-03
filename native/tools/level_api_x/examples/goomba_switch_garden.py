#!/usr/bin/env python3
"""Example 2: a switch puzzle on Good Egg's Soramame planet (the broad bean), every mission.

A ring of coins around the arrival point, a Goomba guarding the way, and a 1-Up Mushroom that only
appears once the Goomba is defeated: the Goomba writes a fresh zone switch ("new") and the mushroom
reads the same one ("@goomba.SW_DEAD"). A grid of Star Bits floats over the arrival point. Every
placement is snapped to the planet along its own gravity. Ends with the checks and a dry-run probe of
a mistake (the mushroom pointed at a switch nobody writes) to show what an agent gets back.

    native/tools/level_api_x/examples/goomba_switch_garden.py [--ws build/ws/garden] [--disc ...] [--save]
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ws", default=str(ROOT / "build/ws/garden"))
    ap.add_argument("--disc", default=str(ROOT / "build/game-data/RMGE01"))
    ap.add_argument("--save", action="store_true", help="save the patch mod 'Goomba Switch Garden'")
    args = ap.parse_args()

    lv = Level.init(args.ws, disc=args.disc, galaxy="EggStarGalaxy", force=True)
    arrival = lv.get({"class": "Mario", "zone": "SoramameZone"})["pos"]
    up = [-v for v in lv.gravity(arrival)["down"]]
    # A direction along the ground: any vector not parallel to up, flattened.
    side = [1, 0, 0] if abs(up[0]) < 0.9 else [0, 0, 1]
    dot = sum(side[i] * up[i] for i in range(3))
    ahead = [side[i] - up[i] * dot for i in range(3)]
    n = sum(v * v for v in ahead) ** 0.5
    ahead = [v / n for v in ahead]

    with lv.batch(key="garden", label="Goomba switch garden") as b:
        b.array("Coin", "ring", center=arrival, radius=450, count=10, on_ground=True, height=70, label="ring")
        b.add("Kuribo", add(arrival, ahead, 900), on_ground=True, switches={"SW_DEAD": "new"}, label="goomba")
        b.add("BenefitItemOneUp", add(arrival, ahead, 1250), on_ground=True, height=50,
              switches={"SW_APPEAR": "@goomba.SW_DEAD"}, label="oneup")
        b.array("StarPiece", "grid", origin=add(arrival, up, 450), size=[3, 3], spacing=160, label="bits")
    result = b.result
    goomba = lv.get("@goomba")
    oneup = lv.get("@oneup")
    print(json.dumps({"created": len(result["created"]), "goomba": goomba["switches"], "oneup": oneup["switches"],
                      "checks_introduced": result["checks_introduced"]}, indent=1))

    # What a mistake looks like: a mushroom waiting on a switch nobody turns on (dry run: nothing changes).
    probe = lv.apply([{"op": "set", "ref": "@oneup", "switches": {"SW_APPEAR": 77}}], dry_run=True)
    print(json.dumps({"dry_run_checks_introduced": probe["checks_introduced"]}, indent=1))
    print(json.dumps(lv.checks(), indent=1))
    if args.save:
        print(json.dumps(lv.save("Goomba Switch Garden"), indent=1))
    lv.close()


if __name__ == "__main__":
    main()
