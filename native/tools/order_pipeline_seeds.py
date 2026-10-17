#!/usr/bin/env python3
"""Hot-first load order for the pipeline seeds (native/tools/PIPELINE_SEEDING.md, "Hot-first seed order").

A stage manifest is compiled in `first_frame_used` order when the stage begins. The bundled order interleaves the
configs a stage actually draws with hundreds of shared baseline rows it never draws, so on a cold first visit the
drawn set is complete only after 400-1,500 compiles instead of the 100-330 it needs. This tool rewrites only the
`first_frame_used` column (rows and configs are unchanged):

  * stage databases: configs observed drawn in that stage (any scenario) first, ranked by their earliest first-use
    position across runs (ties: drawn in more runs first); then the remaining observed rows in their old order;
    then speculative rows (0xFFFFFFFF), untouched.
  * __global__.db (the background backlog): configs drawn in the most stages first (whatever stage comes next is
    likely to need them), then by earliest first use; then the remaining observed rows; then speculative rows.

  order_pipeline_seeds.py observe OUT.json SWEEP_DIR...   per-run first-use sequences from clean PASS sweep runs
                                                         ("[gx stage config] stage=S config=H" lines in app.log)
  order_pipeline_seeds.py apply SEED_DIR OBS.json... [--exclude-scenario N] [--stages-only] [--create STAGE...]
                                                         rewrite first_frame_used in place; --create writes
                                                         STAGE.db (its observed rows, from __global__.db) when the
                                                         seeds have none, e.g. Unknown (boot/logo scenes), whose
                                                         gate otherwise waits for every observed config
  order_pipeline_seeds.py add-rows SEED_DIR HARVEST.db STAGES.json OUT_OBS.json
                                                         insert configs seen in real play (an Aurora pipeline_cache
                                                         db of verbatim rows) into __global__.db and every attributed
                                                         <Stage>.db, and write OUT_OBS.json so `apply` ranks them hot
                                                         there. STAGES.json: {"stages": {"<config hex16>": [Stage...]},
                                                         "weights": {"<config hex16>": skipped draws}} (weights
                                                         optional: heavier misses rank first)
Hot rows get ranks 0..H-1 and the other observed rows 1,000,000 + rank: the app's stage gate waits only for rows
below 1,000,000 (the configs that stage is known to draw), not for the whole manifest. Speculative rows stay
0xFFFFFFFF. Re-run `apply` after any seed regeneration, then repack with seedpack.py.
"""
import argparse
import json
import os
import re
import sqlite3
import sys
from collections import defaultdict

NEVER = 0xFFFFFFFF
# Hot rows get first_frame_used < HOT_LIMIT, other observed rows HOT_LIMIT + rank. The app's stage gate waits only
# for rows below HOT_LIMIT (pipeline_stage.inc, kPetariHotLimit).
HOT_LIMIT = 1_000_000
GLOBAL = "__global__"
LINE = re.compile(rb"\[gx stage config\] stage=(\S+) config=([0-9a-f]{16}) phase=\S+")
PASS = ("PASS", "PASS_WARN")


def observe(out, sweeps):
    runs = {}
    for sweep in sweeps:
        root = os.path.join(sweep, "runs")
        for name in sorted(os.listdir(root)):
            run = os.path.join(root, name)
            try:
                with open(os.path.join(run, "result.json")) as stream:
                    result = json.load(stream)
                with open(os.path.join(run, "app.log"), "rb") as stream:
                    log = stream.read()
            except (OSError, ValueError):
                continue
            if result.get("outcome") not in PASS:
                continue
            sequences = defaultdict(list)
            for match in LINE.finditer(log):
                sequences[match.group(1).decode()].append(match.group(2).decode())
            runs[f"{os.path.basename(os.path.normpath(sweep))}/{name}"] = {
                "stage": result.get("stage"), "scenario": result.get("scenario"), "sequences": sequences}
    with open(out, "w") as stream:
        json.dump({"runs": runs}, stream, indent=0, sort_keys=True)
    return len(runs)


def signed(value):
    return value - (1 << 64) if value >= 1 << 63 else value


def hot_sets(observations, exclude_scenario=None):
    """{stage: {hash: (earliest position, -runs)}} and {hash: set(stages)}."""
    best = defaultdict(dict)
    counts = defaultdict(lambda: defaultdict(int))
    stages_of = defaultdict(set)
    for obs in observations:
        for run in obs["runs"].values():
            if exclude_scenario is not None and run.get("scenario") == exclude_scenario:
                continue
            for stage, sequence in run["sequences"].items():
                seen = set()
                for position, text in enumerate(sequence):
                    key = signed(int(text, 16))
                    if key in seen:
                        continue
                    seen.add(key)
                    counts[stage][key] += 1
                    stages_of[key].add(stage)
                    previous = best[stage].get(key)
                    best[stage][key] = position if previous is None else min(previous, position)
    return {s: {k: (p, -counts[s][k]) for k, p in keys.items()} for s, keys in best.items()}, stages_of


def reorder(path, score, shared=None):
    """Rewrites first_frame_used: scored (hot) GX rows first, then the other observed rows (those drawn in more
    stages first, else in their old order), speculative rows last."""
    db = sqlite3.connect(path)
    try:
        rows = db.execute("select type, hash, first_frame_used from pipeline_cache order by first_frame_used, rowid").fetchall()
        hot = sorted((r for r in rows if r[0] == 1 and r[1] in score), key=lambda r: (score[r[1]], r[1]))
        hot_keys = {(r[0], r[1]) for r in hot}
        rest = [r for r in rows if (r[0], r[1]) not in hot_keys and r[2] != NEVER]
        if shared:
            rest.sort(key=lambda r: -shared.get(r[1], 0))  # stable: old order within equal counts
        if len(hot) >= HOT_LIMIT or len(rest) >= HOT_LIMIT:
            raise ValueError(f"{path}: too many rows to rank")
        updates = [(i, t, h) for i, (t, h, _) in enumerate(hot)]
        updates += [(HOT_LIMIT + i, t, h) for i, (t, h, _) in enumerate(rest)]
        db.executemany("update pipeline_cache set first_frame_used=? where type=? and hash=?", updates)
        db.commit()
        return len(hot), len(rest), len(rows)
    finally:
        db.close()


def create(seed_dir, stage, keys):
    """STAGE.db holding the given GX configs, copied from __global__.db (the pack requires that subset)."""
    path = os.path.join(seed_dir, stage + ".db")
    if os.path.exists(path):
        return False
    source = sqlite3.connect(f"file:{os.path.join(seed_dir, GLOBAL + '.db')}?mode=ro", uri=True)
    try:
        schema = [r[0] for r in source.execute("select sql from sqlite_master where sql is not null order by rowid")]
        version = source.execute("select value from aurora_schema").fetchall()
        rows = [r for r in source.execute("select * from pipeline_cache where type=1") if r[1] in keys]
    finally:
        source.close()
    if not rows:
        return False
    db = sqlite3.connect(path)
    try:
        for statement in schema:
            db.execute(statement)
        db.executemany("insert into aurora_schema values(?)", version)
        db.executemany(f"insert into pipeline_cache values({','.join('?' * len(rows[0]))})", rows)
        db.commit()
    finally:
        db.close()
    return True


def add_rows(seed_dir, harvest, stages_path, out_observations):
    """Insert harvested rows (global + attributed stages) and write their observations. Returns counts."""
    with open(stages_path) as stream:
        meta = json.load(stream)
    attributed = {signed(int(k, 16)): v for k, v in meta.get("stages", {}).items()}
    weights = {signed(int(k, 16)): w for k, w in meta.get("weights", {}).items()}
    source = sqlite3.connect(f"file:{harvest}?mode=ro", uri=True)
    try:
        rows = source.execute("select type, hash, config_version, config_size, config from pipeline_cache").fetchall()
    finally:
        source.close()
    global_path = os.path.join(seed_dir, GLOBAL + ".db")
    db = sqlite3.connect(global_path)
    try:
        versions = dict(db.execute("select type, max(config_version) from pipeline_cache group by type").fetchall())
        sizes = dict(db.execute("select type, max(config_size) from pipeline_cache group by type").fetchall())
    finally:
        db.close()
    accepted, rejected = [], 0
    for t, h, v, size, blob in rows:
        if t not in versions or v != versions[t] or size != len(blob) or size != sizes[t]:
            rejected += 1
            continue
        accepted.append((t, h, v, size, blob))
    counts = {"rows": len(rows), "accepted": len(accepted), "rejected": rejected, "new_global": 0, "stage_rows": 0}

    def insert(path, chosen):
        db = sqlite3.connect(path)
        try:
            before = db.total_changes
            db.executemany("insert or ignore into pipeline_cache values(?,?,?,?,?,0)", chosen)
            db.commit()
            return db.total_changes - before
        finally:
            db.close()

    counts["new_global"] = insert(global_path, accepted)
    by_stage = defaultdict(list)
    for row in accepted:
        for stage in attributed.get(row[1], []):
            by_stage[stage].append(row)
    for stage, chosen in by_stage.items():
        path = os.path.join(seed_dir, stage + ".db")
        if os.path.exists(path):
            counts["stage_rows"] += insert(path, chosen)
    # One synthetic run per stage: heavier misses first, so `apply` ranks them hot there.
    runs = {}
    for stage, chosen in sorted(by_stage.items()):
        keys = sorted((row[1] for row in chosen), key=lambda k: -weights.get(k, 0))
        runs[f"harvest/{stage}"] = {"stage": stage, "scenario": None,
                                    "sequences": {stage: [f"{k & ((1 << 64) - 1):016x}" for k in keys]}}
    with open(out_observations, "w") as stream:
        json.dump({"runs": runs}, stream, indent=0, sort_keys=True)
    return counts


def apply(seed_dir, observation_paths, exclude_scenario=None, stages_only=False, create_stages=()):
    observations = []
    for path in observation_paths:
        with open(path) as stream:
            observations.append(json.load(stream))
    per_stage, stages_of = hot_sets(observations, exclude_scenario)
    for stage in create_stages:
        create(seed_dir, stage, set(per_stage.get(stage, {})))
    report = {}
    for name in sorted(os.listdir(seed_dir)):
        if not name.endswith(".db"):
            continue
        stage = name[:-3]
        if stage == GLOBAL:
            if stages_only:
                continue
            earliest = {}
            for keys in per_stage.values():
                for key, (position, _) in keys.items():
                    earliest[key] = min(earliest.get(key, position), position)
            score = {k: (-len(stages_of[k]), earliest[k]) for k in earliest}
        else:
            score = per_stage.get(stage)
            if not score:
                continue
        shared = {k: len(v) for k, v in stages_of.items()}
        report[stage] = dict(zip(("hot", "observed_rest", "rows"), reorder(os.path.join(seed_dir, name), score, shared)))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("observe"); p.add_argument("out"); p.add_argument("sweeps", nargs="+")
    p = sub.add_parser("apply"); p.add_argument("seed_dir"); p.add_argument("observations", nargs="+")
    p.add_argument("--exclude-scenario", type=int); p.add_argument("--stages-only", action="store_true")
    p.add_argument("--create", nargs="*", default=[])
    p = sub.add_parser("add-rows"); p.add_argument("seed_dir"); p.add_argument("harvest"); p.add_argument("stages")
    p.add_argument("out_observations")
    args = parser.parse_args()
    if args.command == "add-rows":
        print(json.dumps(add_rows(args.seed_dir, args.harvest, args.stages, args.out_observations)))
    elif args.command == "observe":
        print(f"{observe(args.out, args.sweeps)} clean runs")
    else:
        report = apply(args.seed_dir, args.observations, args.exclude_scenario, args.stages_only, args.create)
        for stage, counts in report.items():
            print(f"{stage}: {counts['hot']} hot / {counts['observed_rest']} other observed / {counts['rows']} rows")


if __name__ == "__main__":
    sys.exit(main())
