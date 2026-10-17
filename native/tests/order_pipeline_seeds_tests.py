#!/usr/bin/env python3
"""order_pipeline_seeds.py: hot-first ranks, untouched speculative rows, created stage DBs, and a seedpack round trip."""
import json
import os
import sqlite3
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "native", "tools"))
import order_pipeline_seeds as order  # noqa: E402
import seedpack  # noqa: E402

NEVER = 0xFFFFFFFF


def make_db(path, rows):
    db = sqlite3.connect(path)
    db.executescript(seedpack.SCHEMA)
    db.execute("insert into aurora_schema values(1)")
    db.executemany("insert into pipeline_cache values(?,?,?,?,?,?)", rows)
    db.commit()
    db.close()


def row(h, ffu, t=1):
    blob = bytes([h % 251]) * (8 if t == 0 else 40)
    return (t, h, 5 if t == 0 else 13, len(blob), blob, ffu)


def load(path):
    db = sqlite3.connect(path)
    try:
        return {(t, h): f for t, h, f in db.execute("select type, hash, first_frame_used from pipeline_cache")}
    finally:
        db.close()


def order_of(path):
    db = sqlite3.connect(path)
    try:
        return [h for (h,) in db.execute("select hash from pipeline_cache where type=1 order by first_frame_used, rowid")]
    finally:
        db.close()


def hexkey(h):
    return f"{h & ((1 << 64) - 1):016x}"


def main():
    with tempfile.TemporaryDirectory() as tmp:
        seeds = os.path.join(tmp, "seeds")
        os.makedirs(seeds)
        big = -(2 ** 62) - 5  # negative signed hash: logs print it as unsigned hex
        stage_rows = [row(1, 10), row(2, 20), row(3, 30), row(big, 40), row(5, NEVER), row(6, NEVER), row(7, 0, t=0)]
        make_db(os.path.join(seeds, "__global__.db"), stage_rows + [row(8, 50), row(9, NEVER)])
        make_db(os.path.join(seeds, "StageA.db"), stage_rows)
        make_db(os.path.join(seeds, "StageB.db"), [row(1, 10), row(8, 50)])
        observations = {"runs": {
            # StageA draws big, then 3, then a speculative row 6; Unknown draws 2 (no Unknown.db yet).
            "s/StageA-s1": {"stage": "StageA", "scenario": 1,
                            "sequences": {"StageA": [hexkey(big), hexkey(3), hexkey(6), hexkey(big)], "Unknown": [hexkey(2)]}},
            # Scenario 2 draws 3 first: earliest position wins, and 3 is seen in two runs.
            "s/StageA-s2": {"stage": "StageA", "scenario": 2, "sequences": {"StageA": [hexkey(3), hexkey(1)]}},
            "s/StageB-s1": {"stage": "StageB", "scenario": 1, "sequences": {"StageB": [hexkey(8), hexkey(1)]}},
        }}
        obs_path = os.path.join(tmp, "obs.json")
        with open(obs_path, "w") as stream:
            json.dump(observations, stream)

        report = order.apply(seeds, [obs_path], create_stages=["Unknown", "StageA"])
        assert report["StageA"] == {"hot": 4, "observed_rest": 2, "rows": 7}, report["StageA"]
        # Hot first (3: position 0 in two runs; big: position 0 in one; 1: position 1; 6: position 2), then the
        # other observed rows (2 is drawn in a stage, Unknown, so before the Clear row), then speculative 5.
        assert order_of(os.path.join(seeds, "StageA.db")) == [3, big, 1, 6, 2, 5], order_of(os.path.join(seeds, "StageA.db"))
        a = load(os.path.join(seeds, "StageA.db"))
        assert a[(1, 3)] == 0 and a[(1, big)] == 1 and a[(1, 1)] == 2 and a[(1, 6)] == 3, a
        assert a[(1, 2)] == order.HOT_LIMIT and a[(0, 7)] == order.HOT_LIMIT + 1, a
        assert a[(1, 5)] == NEVER, "speculative rows must stay speculative"
        # Global: drawn in the most stages first (1: StageA and StageB), then by earliest first use.
        g = order_of(os.path.join(seeds, "__global__.db"))
        assert g[0] == 1 and set(g[1:6]) == {3, big, 6, 2, 8} and g[-1] in (5, 9), g
        # Unknown.db is created from __global__.db; an existing StageA.db is never replaced.
        assert load(os.path.join(seeds, "Unknown.db")) == {(1, 2): 0}
        # The pack format keeps the new ranks.
        pack = os.path.join(tmp, "p.v13.xz")
        seedpack.pack(seeds, pack)
        out = os.path.join(tmp, "expanded")
        seedpack.expand(pack, out)
        assert seedpack.verify(seeds, out) == 0
        assert order_of(os.path.join(out, "StageA.db")) == [3, big, 1, 6, 2, 5]
        # Re-applying is idempotent.
        order.apply(seeds, [obs_path], create_stages=["Unknown"])
        assert load(os.path.join(seeds, "StageA.db")) == a
        # --exclude-scenario drops that scenario's runs (StageA then has only 3 and 1 hot).
        report = order.apply(seeds, [obs_path], exclude_scenario=1)
        assert report["StageA"]["hot"] == 2, report["StageA"]
        # add-rows: harvested real-play configs join __global__ and their stage, and rank hot there.
        harvest = os.path.join(tmp, "harvest.db")
        make_db(harvest, [row(77, 0), row(78, 0), row(79, 0), (1, 80, 12, 40, bytes(40), 0)])  # 80: wrong version
        with open(os.path.join(tmp, "stages.json"), "w") as stream:
            json.dump({"stages": {hexkey(77): ["StageB"], hexkey(78): ["StageB", "Missing"]},
                       "weights": {hexkey(78): 500, hexkey(77): 3}}, stream)
        harvest_obs = os.path.join(tmp, "harvest-obs.json")
        counts = order.add_rows(seeds, harvest, os.path.join(tmp, "stages.json"), harvest_obs)
        assert counts == {"rows": 4, "accepted": 3, "rejected": 1, "new_global": 3, "stage_rows": 2}, counts
        assert (1, 79) in load(os.path.join(seeds, "__global__.db")) and (1, 80) not in load(os.path.join(seeds, "__global__.db"))
        order.apply(seeds, [obs_path, harvest_obs])
        # StageB: 78 (heavier miss) and 77 rank with its observed draws (8 first: earliest in its own run).
        assert order_of(os.path.join(seeds, "StageB.db"))[:2] == [8, 78], order_of(os.path.join(seeds, "StageB.db"))
        assert set(order_of(os.path.join(seeds, "StageB.db"))[:4]) == {8, 78, 1, 77}
        seedpack.pack(seeds, pack)
        assert not seedpack.check(pack), "pack with harvested rows must pass the check"
    print("order_pipeline_seeds: add-rows, hot ranks, rest offset, speculative kept, created stage, pack round trip, holdout pass")


if __name__ == "__main__":
    main()
