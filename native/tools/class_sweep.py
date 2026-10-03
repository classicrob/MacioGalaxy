#!/usr/bin/env python3
"""Class sweep (level editor spike S2): spawn every NameObjFactory creator in a running stage.

For each creator the in-game driver (src/Game/Scene/ClassSweep.cpp, PETARI_CLASS_SWEEP_JOBS) mounts the
class's archives, creates it from a synthesized placement row with empty links, lets it run a few frames,
reports ok / dead / invisible / nonactor, and kills it. This harness runs the driver in background smoke
processes (one batch of classes per process, through build/locked-app.sh), survives crashes, asserts and
hangs by attributing them to the batch's in-flight class and resuming after it, confirms every failure
alone in a fresh process, then bisects the link fields of the failures.

  python3 native/tools/class_sweep.py list
  python3 native/tools/class_sweep.py run     --name s2 [--stages EggStarGalaxy:1,...] [--classes A,B] [--batch 20]
  python3 native/tools/class_sweep.py bisect  --name s2          # required links for every failing class
  python3 native/tools/class_sweep.py report  --name s2          # sweep.json + SWEEP.md under docs/dev/level-editor/catalog

State lives under build/class-sweep/<name>/<stage>/ (results.jsonl is the resume point: a class with a
record is never run again; delete the file, or pass --rerun, to start over). See docs/dev/level-editor/catalog/SWEEP.md.
"""
import argparse
import collections
import datetime
import json
import os
import re
import signal
import subprocess
import sys
import time
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parents[1]
FACTORY = REPO / "src/Game/NameObj/NameObjFactory.cpp"
CATALOG = REPO / "docs/dev/level-editor/catalog"

# Stage, scenario, why. Different stages have different resident archives, scene objects and gravity.
STAGES = [("EggStarGalaxy", 1, "Good Egg: the plain grass planet"),
          ("OceanRingGalaxy", 1, "Gobblegut/Ocean Ring: water, sea gravity"),
          ("KoopaBattleVs1Galaxy", 1, "Bowser's first battle: boss stage, small arena")]

# Every field of the ObjInfo schema that points at something else. Keep in step with sLinkFields in ClassSweep.cpp.
LINK_FIELDS = ["CameraSetId", "SW_APPEAR", "SW_DEAD", "SW_A", "SW_B", "SW_SLEEP", "MessageId", "CastId", "ViewGroupId",
               "CommonPath_ID", "ClippingGroupId", "GroupId", "DemoGroupId", "MapParts_ID"]
PASS = {"ok", "ok-nomodel", "nonactor"}
AREA_FACTORIES = ("createCenterOriginCube", "createBaseOriginCube", "createSphere", "createBaseOriginCylinder")
AREA_SCALE = "scale_x=0.02,scale_y=0.02,scale_z=0.02"  # 20 units: areas must not swallow Mario


def main_checkout():
    """The main checkout: lock scripts, disc data and the fixture source live there, not in worktrees."""
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=REPO,
                            capture_output=True, text=True, check=True).stdout.strip()
    return Path(common).parent


# --- the class list ------------------------------------------------------------------------------

def list_classes(text=None):
    """[{class, creator, archive, area_like}] for every entry of NameObjFactory's cCreateTable."""
    text = FACTORY.read_text() if text is None else text
    start = text.index("const NameObjFactory::Name2CreateFunc cCreateTable[]")
    body = text[start:text.index("};", start)]
    out = []
    for name, creator, archive in re.findall(r'\{\s*"([^"]+)",\s*([^,}]+?)(?:,\s*(nullptr|"[^"]*"))?\s*,?\s*\}', body):
        creator = creator.strip()
        area = creator.startswith(AREA_FACTORIES) or bool(re.search(r"(Area|Gravity)$", name))
        out.append({"class": name, "creator": creator, "archive": None if archive in ("", "nullptr") else archive.strip('"'),
                    "area_like": area})
    return out


# --- jobs, results ---------------------------------------------------------------------------------

def job_key(cls, mode, overrides):
    return f"{cls}|{mode}|{overrides}"


def job_line(job):
    return "\t".join((job["class"], job["mode"], job["overrides"]))


def placement_counts(cache=None):
    """{lower-case class name: placements} over every placement table of every stage archive on the disc (ObjInfo,
    AreaObjInfo, GravityInfo, ... : any table with a name column), cached in build/class-sweep/placement-counts.json.
    The sweep runs the most-placed classes first: those are the ones people will place in the editor."""
    cache = Path(cache or REPO / "build/class-sweep/placement-counts.json")
    if cache.is_file():
        return json.loads(cache.read_text())
    sys.path.insert(0, str(TOOLS))
    import stage_sweep as sweep
    from collect_pipeline_seed_inputs import field_hash, string
    name_hash = field_hash("name")
    counts = collections.Counter()
    files = main_checkout() / "build/game-data/RMGE01/files/StageData"
    for arc in sorted(files.glob("*.arc")):
        try:
            tree = list(sweep.archive_tree(arc.read_bytes()))
        except Exception:  # an archive the reader does not know: skip it, the count is an ordering heuristic
            continue
        for path, data in tree:
            if "/placement/" not in path or len(data) < 16:
                continue
            count, fields, offset, stride = (int.from_bytes(data[i:i + 4], "big") for i in (0, 4, 8, 12))
            pool = data[offset + count * stride:]
            for field in range(fields):
                entry = 16 + field * 12
                if int.from_bytes(data[entry:entry + 4], "big") == name_hash and data[entry + 11] == 6:
                    pos = int.from_bytes(data[entry + 8:entry + 10], "big")
                    for row in range(count):
                        record = data[offset + row * stride:offset + (row + 1) * stride]
                        counts[string(pool, int.from_bytes(record[pos:pos + 4], "big")).lower()] += 1
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(counts, sort_keys=True) + "\n")
    return dict(counts)


def order_by_placement(classes):
    """Most-placed first (ties by name); classes never placed on the disc last."""
    counts = placement_counts()
    return sorted(classes, key=lambda c: (-counts.get(c["class"].lower(), 0), c["class"]))


def make_jobs(classes, mode="empty"):
    return [{"class": c["class"], "mode": mode, "overrides": AREA_SCALE if c["area_like"] else ""} for c in classes]


def load_results(path):
    """{key: record}; the last record for a key wins (a confirm run replaces the batch's)."""
    out = {}
    if path.is_file():
        for line in path.read_text().splitlines():
            if line.strip():
                record = json.loads(line)
                out[record["key"]] = record
    return out


def append_result(path, record):
    with path.open("a") as stream:
        stream.write(json.dumps(record, sort_keys=True) + "\n")


# --- log parsing -----------------------------------------------------------------------------------

BACKTRACE = re.compile(r"^\d+\s+(\S+)\s+0x[0-9a-f]+\s+(.*?)(?:\s+\+\s+\d+)?$")
NOISE = ("libsystem_", "libdyld", "Crash::", "crashHandler", "signalHandler", "_sigtramp", "abort", "__pthread", "__assert",
         "std::__1", "_os_", "pthread_")


def parse_log(text):
    """What a batch run did, from its log.

    items: {index: {name, began, created, result{k: v}, killed}}; done: BATCH-DONE seen; abort: reason or None;
    crash: details of the native crash report, or None; started: the driver reached its first frame.
    """
    items = {}
    done = False
    abort = None
    started = False
    last_begin = None
    for line in text.splitlines():
        if not line.startswith("[class-sweep] "):
            continue
        body = line[len("[class-sweep] "):]
        word, _, rest = body.partition(" ")
        if word == "start:":
            started = True
        elif word == "BEGIN":
            index, _, name = rest.partition(" ")
            last_begin = int(index)
            items[last_begin] = {"name": name, "created": False, "result": None, "killed": False, "notes": []}
        elif word == "CREATED":
            items[int(rest.split(" ", 1)[0])]["created"] = True
        elif word == "RESULT":
            index, name, tail = rest.split(" ", 2)
            fields = {}
            for token in tail.split(" "):
                key, _, value = token.partition("=")
                fields[key] = value
            items[int(index)]["result"] = fields
        elif word == "KILLED":
            items[int(rest.split(" ", 1)[0])]["killed"] = True
        elif word == "NOTE":
            if last_begin is not None:
                items[last_begin]["notes"].append(rest)
        elif word == "BATCH-DONE":
            done = True
        elif word == "ABORT":
            abort = rest.strip()
    return {"items": items, "done": done, "abort": abort, "started": started, "last_begin": last_begin,
            "crash": parse_crash(text)}


def parse_crash(text):
    marker = "==== Petari native crash report ===="
    crash = None
    if marker in text:
        report = text[text.index(marker):].splitlines()
        signal_line = next((l for l in report if l.startswith("signal:")), "")
        panic = next((l[len("last panic:"):].strip() for l in report if l.startswith("last panic:")), "")
        frames = []
        for line in report:
            m = BACKTRACE.match(line.strip())
            if m and not any(n in m.group(1) + m.group(2) for n in NOISE):
                frames.append(m.group(2))
        crash = {"signal": signal_line, "panic": panic, "top_frames": frames[:8]}
    uncaught = re.findall(r"terminating due to uncaught exception of type (\S+?)(?:: |$)", text, re.M)
    fatal = [l for l in text.splitlines() if l.startswith("OSFatal:")][:3]
    if crash is None and (uncaught or fatal or "Native heap allocation failed" in text):
        crash = {"signal": "", "panic": fatal[0] if fatal else "", "top_frames": []}
    if crash is not None:
        crash["uncaught_exception"] = uncaught[-1] if uncaught else None
        crash["heap_failure"] = "Native heap allocation failed" in text
        crash["os_fatal"] = fatal
    return crash


def fail_status(crash, stalled, phase):
    """The status of the class that was in flight when the process died."""
    if stalled:
        return "hang"
    if crash and crash.get("heap_failure"):
        return "oom"
    if crash and (crash.get("panic") or crash.get("os_fatal")):
        return "assert"
    if crash and crash.get("uncaught_exception"):
        return "exception"
    return "crash"


def record_for(job, index, item, parsed, stalled, exit_status, batch_size):
    """The results.jsonl record for one job of a batch, or None when it never began (it runs again)."""
    base = {"key": job_key(job["class"], job["mode"], job["overrides"]), "class": job["class"], "mode": job["mode"],
            "overrides": job["overrides"], "batch_size": batch_size}
    if item is None:
        return None
    result = item["result"]
    if result is not None:
        status = result["status"]
        base.update(status=status, phase="done", metrics={k: v for k, v in result.items() if k not in ("status", "actor")})  # actor names are Shift-JIS
        if not item["killed"] and index == parsed["last_begin"] and not parsed["done"] and not parsed["abort"]:
            base.update(status=fail_status(parsed["crash"], stalled, "kill"), phase="kill", observed=status)
            base["crash"] = parsed["crash"]
        return base
    if index == parsed["last_begin"] and not parsed["done"]:
        if parsed["abort"]:
            base.update(status="kills-player", phase="observe", detail=parsed["abort"])
            return base
        phase = "observe" if item["created"] else "create"
        base.update(status=fail_status(parsed["crash"], stalled, phase), phase=phase, exit=exit_status)
        if base["status"] == "crash" and parsed["crash"] is None and exit_status == 1:
            base["status"] = "kills-player"  # the smoke ended the run: Mario died, no crash report
        base["crash"] = parsed["crash"]
        return base
    return None


# --- running the app -----------------------------------------------------------------------------------

def run_process(command, env, log_path, total_seconds, stall_seconds):
    """Runs command, returns (exit_status, stalled, startup_seconds): startup is the app's first log line (lock wait excluded) to the
    driver's first frame, None if it never began. The stall watchdog starts once the driver has begun (before that
    the total alarm applies); a stalled process is killed as a group."""
    with log_path.open("wb") as stream:
        proc = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        stalled = False
        began = None
        launched = time.time()
        launched_app = None  # when the app's first line appeared: lock waiting is not startup
        startup = None
        deadline = time.time() + total_seconds + 3600  # the lock wait is not counted against the run
        while True:
            try:
                return proc.wait(timeout=1 if began is None else 5), stalled, startup
            except subprocess.TimeoutExpired:
                pass
            if time.time() > deadline:
                stalled = True
            elif log_path.stat().st_size > 0 and began is None:
                head = log_path.read_bytes()[-2_000_000:]
                if launched_app is None and b"PETARI FIXTURE" in head:
                    launched_app = time.time()  # the lock was granted and the app is running
                if b"[class-sweep] start:" in head:
                    began = time.time()
                    startup = began - (launched_app or launched)
            elif began is not None:
                if time.time() - log_path.stat().st_mtime > stall_seconds:
                    stalled = True
            if stalled:
                for sig in (signal.SIGTERM, signal.SIGKILL):
                    try:
                        os.killpg(proc.pid, sig)
                    except ProcessLookupError:
                        break
                    try:
                        return proc.wait(timeout=10), True, startup
                    except subprocess.TimeoutExpired:
                        continue
                return proc.wait(), True, startup


def warm_args(app, user=None):
    import warm_seed
    a = argparse.Namespace(app=Path(app), seed_root=str(main_checkout() / "build/shader-seed"), stages=warm_seed.DEFAULT_STAGES,
                           owner="class-sweep", timeout=900, idle=900, force=False, user=user)
    a.sha = warm_seed.sha256_of(a.app)
    return a


def run_batch(args, stage, scenario, jobs, batch_dir, tag):
    """One app process over `jobs`. Returns (records, parsed, status) where records are new results."""
    main = main_checkout()
    batch_dir.mkdir(parents=True, exist_ok=True)
    jobs_file = batch_dir / f"{tag}.jobs.tsv"
    jobs_file.write_text("\n".join(job_line(j) for j in jobs) + "\n")
    user = batch_dir / f"{tag}.user"
    if user.exists():
        subprocess.run(["rm", "-rf", str(user)], check=True)
    sys.path.insert(0, str(TOOLS))
    import stage_sweep as sweep
    sweep.load_fixture_module().create(main / "build/observatory-user-2", user, "stage")

    class A:
        pass
    a = A()
    a.app = Path(args.app) if args.app else REPO / "build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari"
    a.disc = main / "build/game-data/RMGE01"
    a.frames = args.frames
    a.idle_frames = args.frames  # the driver ends the process; the smoke must not end first
    a.pipeline_policy = "default"
    command, env_extra = sweep.app_command(a, user, stage, scenario)
    env = dict(os.environ, **env_extra)
    env.update(PETARI_LOCK_CLASS="functional", PETARI_EDITOR="1", PETARI_CLASS_SWEEP_JOBS=str(jobs_file),
               PETARI_CLASS_SWEEP_OBSERVE=str(args.observe))
    if not args.no_warm_seed:
        # Warm shader seed (native/tools/warm_seed.py): published on first use for this build; the run's databases are
        # private copy-on-write clones, so batches stay isolated.
        import warm_seed
        seed = warm_seed.ensure(warm_args(a.app))
        warm_seed.prepare(warm_args(a.app, user=user))
        env["PETARI_SHADER_SEED_DIR"] = str(seed)
    # taskpolicy -b: macOS background QoS, so the sweep only uses spare CPU and never slows other workers' builds and smokes.
    full = [str(main / "build/locked-app.sh"), args.owner, "/usr/bin/perl", "-e", "alarm shift; exec @ARGV or die",
            str(args.timeout)] + ([] if args.normal_priority else ["/usr/sbin/taskpolicy", "-b"]) + command
    log = batch_dir / f"{tag}.log"
    status, stalled, startup = run_process(full, env, log, args.timeout, args.stall)
    (batch_dir / f"{tag}.startup").write_text("%s\n" % ("none" if startup is None else f"{startup:.1f}"))
    # The private shader/Dawn cache is ~1.9 GB per process and worthless afterwards: only logs, jobs and Crashes stay.
    subprocess.run(["rm", "-rf", str(user / "cache")], check=False)
    parsed = parse_log(log.read_text(errors="replace"))
    records = []
    for index, job in enumerate(jobs):
        # The first class of a batch ran in a fresh process: its failure needs no confirmation run.
        record = record_for(job, index, parsed["items"].get(index), parsed, stalled, status, 1 if index == 0 else len(jobs))
        if record:
            record.update(stage=stage, scenario=scenario)
            item = parsed["items"][index]
            if item["notes"]:
                record["notes"] = item["notes"]
            if not args.keep_logs and not parsed["crash"] and not stalled:
                pass
            records.append(record)
    return records, parsed, status, stalled


def run_stage(args, stage, scenario, jobs):
    out = Path(args.out_root) / args.name / stage
    out.mkdir(parents=True, exist_ok=True)
    results_path = out / "results.jsonl"
    if args.rerun and results_path.exists():
        results_path.unlink()
    results = load_results(results_path)
    pending = [j for j in jobs if job_key(j["class"], j["mode"], j["overrides"]) not in results]
    if args.infer_known_bad:
        # Classes that failed on another stage are not run here: their record is copied from that stage, marked inferred.
        for other in sorted((Path(args.out_root) / args.name).glob("*/results.jsonl")):
            if other.parent.name == stage:
                continue
            for record in load_results(other).values():
                key = record["key"]
                if record["mode"] == "empty" and record["status"] not in PASS and key not in results and any(j["class"] == record["class"] and job_key(j["class"], j["mode"], j["overrides"]) == key for j in pending):
                    inferred = {k: record[k] for k in ("key", "class", "mode", "overrides", "status", "phase", "crash") if k in record}
                    inferred.update(batch_size=1, stage=stage, scenario=scenario, inferred_from=other.parent.name)
                    append_result(results_path, inferred)
                    results[key] = inferred
        pending = [j for j in pending if job_key(j["class"], j["mode"], j["overrides"]) not in results]
    print(f"[{stage}] {len(jobs)} jobs, {len(jobs) - len(pending)} already done, {len(pending)} to run", flush=True)
    batch_no = len(list(out.glob("batch*.log")))
    infra_failures = 0
    retry = []
    while pending:
        # Classes that failed on another stage go last: the safe ones then rarely restart the app.
        bad = known_bad(args, stage)
        pending.sort(key=lambda j: (job_key(j["class"], j["mode"], j["overrides"]) not in retry, j["class"] in bad and j["mode"] == "empty"))
        batch = pending[:args.batch]
        batch_no += 1
        tag = f"batch{batch_no:04d}"
        t0 = time.time()
        records, parsed, status, stalled = run_batch(args, stage, scenario, batch, out, tag)
        keys = set()
        retry = []
        for record in records:
            if record["status"] not in PASS and record["batch_size"] > 1:
                if zlib.crc32(record["class"].encode()) % args.confirm_every == 0:
                    # Failed behind other classes: it runs again as the first class of the next process (fresh, so its
                    # failure is its own); that run is what gets recorded. Only a sample is confirmed (a crash costs a
                    # ~1 min app restart), enough to estimate how often a neighbour was the cause.
                    retry.append(record["key"])
                    continue
                record["confirm"] = "not-run"  # the in-flight class's own stack is the evidence
            append_result(results_path, record)
            keys.add(record["key"])
        progressed = len(records) if keys else 0
        bad = [r for r in records if r["status"] not in PASS]
        print(f"[{stage}] {tag}: {len(batch)} jobs, {progressed} recorded, {len(bad)} not ok "
              f"({'done' if parsed['done'] else 'abort ' + str(parsed['abort']) if parsed['abort'] else 'stalled' if stalled else 'exit ' + str(status)}) "
              f"{time.time() - t0:.0f}s", flush=True)
        if progressed == 0:
            infra_failures += 1
            if infra_failures >= 3:
                print(f"[{stage}] no progress in 3 batches: see {out}/{tag}.log", file=sys.stderr)
                return False
        else:
            infra_failures = 0
        pending = [j for j in pending if job_key(j["class"], j["mode"], j["overrides"]) not in keys]
        # A batch that aborted (player died) leaves its in-flight class recorded as kills-player; the rest run again.
    confirm_failures(args, stage, scenario, out)
    return True


def known_bad(args, stage):
    """Class names that failed (empty links) in any other stage of this sweep so far."""
    bad = set()
    for other in (Path(args.out_root) / args.name).glob("*/results.jsonl"):
        if other.parent.name != stage:
            bad |= {r["class"] for r in load_results(other).values() if r["mode"] == "empty" and r["status"] not in PASS}
    return bad


def confirm_failures(args, stage, scenario, out):
    """Failures recorded inside a multi-class batch are re-run alone: a class that passes alone was a victim of its
    neighbours (heap, lists), not itself broken."""
    results_path = out / "results.jsonl"
    results = load_results(results_path)
    todo = [r for r in results.values() if r["status"] not in PASS and r["batch_size"] > 1 and r.get("confirm") is None
            and not any(f"{f}=" in r["overrides"] for f in LINK_FIELDS)]
    for record in todo:
        job = {"class": record["class"], "mode": record["mode"], "overrides": record["overrides"]}
        tag = f"confirm-{re.sub(r'[^A-Za-z0-9_]', '_', job['class'])}"
        records, parsed, status, stalled = run_batch(args, stage, scenario, [job], out, tag)
        alone = records[0] if records else {"status": "crash", "phase": "boot"}
        record = dict(record)
        record["confirm"] = {"status": alone["status"], "phase": alone.get("phase"), "crash": alone.get("crash")}
        if alone["status"] in PASS:
            record["note_batch"] = f"{record['status']} in a batch of {record['batch_size']}, ok alone"
            record["status"] = alone["status"]
            record["metrics"] = alone.get("metrics")
        elif alone["status"] != record["status"]:
            record["note_batch"] = f"{record['status']} in a batch, {alone['status']} alone"
            record["status"] = alone["status"]
            record["crash"] = alone.get("crash")
        append_result(results_path, record)
        print(f"[{stage}] confirm {job['class']}: {record['confirm']['status']}", flush=True)


# --- bisecting the links -----------------------------------------------------------------------------------

def harvest_links(stage, scenario):
    """{field: [values]} of every link field in the stage's active root-zone ObjInfo rows, most used first. These are
    the ids that really exist in that zone (rails, switches, groups, cameras), so they are the valid values to try."""
    sys.path.insert(0, str(TOOLS))
    import stage_sweep as sweep
    from collect_pipeline_seed_inputs import field_hash
    main = main_checkout()
    files = main / "build/game-data/RMGE01/files"
    names = LINK_FIELDS + ["name"]
    wanted = {field_hash(n): n for n in names}
    scenario_file = files / "StageData" / stage / f"{stage}Scenario.arc"
    mask = None
    for path, data in sweep.archive_tree(scenario_file.read_bytes()):
        if path.endswith("/scenariodata.bcsv"):
            for row in sweep.bcsv_rows(data):
                if row.get("ScenarioNo") == scenario:
                    mask = row.get(field_hash(stage))
    counts = {n: collections.Counter() for n in LINK_FIELDS}
    for path, data in sweep.archive_tree((files / "StageData" / f"{stage}.arc").read_bytes()):
        layer = re.search(r"/placement/(common|layer[a-z])/objinfo$", path)
        if not layer:
            continue
        if layer[1] != "common" and mask is not None and not mask & (1 << (ord(layer[1][-1]) - ord("a"))):
            continue
        for row in decode_bcsv(data, wanted):
            for field in LINK_FIELDS:
                value = row.get(field)
                if isinstance(value, int) and value >= 0:
                    counts[field][value] += 1
    return {f: [v for v, _ in counts[f].most_common(3)] for f in LINK_FIELDS if counts[f]}


def decode_bcsv(data, names):
    import struct
    u32 = lambda o: int.from_bytes(data[o:o + 4], "big")
    count, fields, offset, stride = u32(0), u32(4), u32(8), u32(12)
    for row in range(count):
        record = data[offset + row * stride:offset + (row + 1) * stride]
        values = {}
        for field in range(fields):
            entry = 16 + field * 12
            key, mask, pos, shift, kind = u32(entry), u32(entry + 4), int.from_bytes(data[entry + 8:entry + 10], "big"), data[entry + 10], data[entry + 11]
            if key not in names or kind == 6:
                continue
            if kind == 0:
                v = (int.from_bytes(record[pos:pos + 4], "big") & mask) >> shift
                values[names[key]] = v - (1 << 32) if v >= 1 << 31 else v
            elif kind == 4:
                v = (int.from_bytes(record[pos:pos + 2], "big") & mask) >> shift
                values[names[key]] = v - (1 << 16) if v >= 1 << 15 else v
            elif kind == 5:
                v = (record[pos] & mask) >> shift
                values[names[key]] = v - (1 << 8) if v >= 1 << 7 else v
        yield values


def overrides_text(pairs, base=""):
    parts = [base] if base else []
    parts += [f"{k}={v}" for k, v in pairs]
    return ",".join(parts)


def parse_tpl_links(text):
    out = {}
    if text and text != "-":
        for item in text.split(","):
            key, _, value = item.partition(":")
            out[key] = int(value)
    return out


class Bisect:
    """Required-links search for one failing class on one stage, as rounds of jobs that all classes run together.

    1. keep: create it from the stage's own row of the class with all its links. If the stage has such a row (the log says
       row=cloned) and it passes, the links in that row are the candidates: drop one per round while the class still passes;
       what is left is required. Passing with a synthesized row says nothing (those links belong to another object).
    2. provision: give every link field a real id harvested from the stage, then drop fields one per round while it passes.
    Verdict: required (fields, source drop-keep or drop-provision) or unresolved (no combination passed)."""

    def __init__(self, record, harvest, base_overrides):
        self.cls = record["class"]
        self.base = base_overrides
        self.harvest = harvest
        self.tpl = {}
        self.phase = "start"
        self.keep = []       # fields currently kept/provisioned
        self.values = {}     # provisioned values
        self.next_drop = 0
        self.verdict = None
        self.tested = []

    def jobs(self):
        """The next job to run for this class, or None when finished."""
        if self.verdict:
            return None
        if self.phase == "start":
            return self._job("keep", [])
        if self.phase == "provision-all":
            self.values = {f: v[0] for f, v in self.harvest.items()}
            self.keep = sorted(self.values)
            if not self.keep:
                self.verdict = {"result": "unresolved", "reason": "the stage has no link ids to try"}
                return None
            return self._job("empty", [(f, self.values[f]) for f in self.keep])
        if self.phase in ("drop-keep", "drop-provision"):
            if self.next_drop >= len(self.keep):
                self.verdict = {"result": "required", "fields": self._fields(), "source": self.phase}
                return None
            test = self.keep[:self.next_drop] + self.keep[self.next_drop + 1:]
            self._test = test
            return self._job_for(test)
        raise AssertionError(self.phase)

    def _fields(self):
        return {f: (self.tpl.get(f) if self.phase == "drop-keep" else self.values[f]) for f in self.keep}

    def _job_for(self, fields):
        if self.phase == "drop-keep":
            return self._job("empty", [(f, "keep") for f in fields])
        return self._job("empty", [(f, self.values[f]) for f in fields])

    def _job(self, mode, pairs):
        job = {"class": self.cls, "mode": mode, "overrides": overrides_text(pairs, self.base)}
        self.current = job
        return job

    def feed(self, record):
        ok = record["status"] in PASS
        self.tested.append({"mode": self.current["mode"], "overrides": self.current["overrides"], "status": record["status"]})
        if self.phase == "start":
            metrics = record.get("metrics") or {}
            if ok and metrics.get("row") == "cloned":
                self.tpl = parse_tpl_links(metrics.get("tpl_links", "-"))
                self.phase = "drop-keep"
                self.keep = sorted(self.tpl)
                self.next_drop = 0
            else:
                self.phase = "provision-all"
        elif self.phase == "provision-all":
            if ok:
                self.phase = "drop-provision"
                self.next_drop = 0
            else:
                self.verdict = {"result": "unresolved", "reason": f"fails with real ids in every link field ({record['status']})"}
        else:
            if ok:
                self.keep = self._test
            else:
                self.next_drop += 1


def bisect_stage(args, stage, scenario):
    out = Path(args.out_root) / args.name / stage
    results = load_results(out / "results.jsonl")
    failing = [r for r in results.values()
               if r["mode"] == "empty" and r["status"] not in PASS and "|empty|" in r["key"]
               and not any(f"{f}=" in r["overrides"] for f in LINK_FIELDS)]
    if args.classes:
        wanted = {c.lower() for c in args.classes.split(",")}
        failing = [r for r in failing if r["class"].lower() in wanted]
    if args.with_template_link:
        # Only classes whose own row in this stage links something (e.g. CommonPath_ID: the rail users).
        failing = [r for r in failing if args.with_template_link in parse_tpl_links((r.get("metrics") or {}).get("tpl_links", "-"))]
    harvest = harvest_links(stage, scenario)
    print(f"[{stage}] bisect {len(failing)} failing classes; link ids per field: "
          + ", ".join(f"{f}={v}" for f, v in harvest.items()), flush=True)
    base = {r["class"]: r["overrides"] for r in failing}
    searches = [Bisect(r, harvest, base[r["class"]]) for r in failing]
    verdicts_path = out / "bisect.json"
    rounds = 0
    while True:
        wave = [(s, s.jobs()) for s in searches]
        wave = [(s, j) for s, j in wave if j]
        if not wave:
            break
        rounds += 1
        print(f"[{stage}] bisect round {rounds}: {len(wave)} variants", flush=True)
        by_key = {job_key(j["class"], j["mode"], j["overrides"]): (s, j) for s, j in wave}
        if not run_stage(args, stage, scenario, [j for _, j in wave]):
            return False
        results = load_results(out / "results.jsonl")
        for key, (search, job) in by_key.items():
            if key in results:
                search.feed(results[key])
            else:
                search.verdict = {"result": "unresolved", "reason": "variant never ran"}
    verdicts = {s.cls: dict(s.verdict, tested=s.tested) for s in searches}
    verdicts_path.write_text(json.dumps(verdicts, indent=1, sort_keys=True) + "\n")
    return True


# --- report --------------------------------------------------------------------------------------------------

FAIL_STATUSES = ("crash", "assert", "oom", "exception", "hang", "kills-player")
SUSPECT_STATUSES = ("nan", "invisible", "dead")


def live_recreate_verdict(results):
    """What the editor should do when it recreates this class live (from a placed row), from the per-stage results.

    unsafe     the process crashed, asserted, hung, ran out of heap or killed Mario creating it with empty links on some stage
               (and no bisect run showed it safe with the row's own links): do not recreate live, fall back to the reload path
    suspect    created but nan / invisible / dead on some stage: allowed, but tell the user it may not show or behave
    safe       ok, ok-nomodel or nonactor on every measured stage
    unmeasured nothing recorded yet (partial sweep)
    needs_links (separate field): the link fields the class needs, when the bisect found them."""
    statuses = [r["status"] for r in results.values()]
    if not statuses:
        return "unmeasured"
    if any(s in FAIL_STATUSES for s in statuses):
        # Safe with its own row's links (bisect `keep` passed on every stage that failed)?
        failed = [r for r in results.values() if r["status"] in FAIL_STATUSES]
        if all(r.get("links", {}).get("result") == "required" and r["links"].get("source") == "drop-keep" for r in failed):
            return "safe"
        return "unsafe"
    return "suspect" if any(s in SUSPECT_STATUSES for s in statuses) else "safe"


SEVERITY = ["crash", "assert", "oom", "exception", "hang", "kills-player", "nan", "invisible", "dead", "ok-nomodel", "nonactor", "ok"]


def build_catalog(args, stages):
    classes = list_classes()
    entries = []
    stage_results = {}
    for stage, scenario, _ in stages:
        out = Path(args.out_root) / args.name / stage
        results = load_results(out / "results.jsonl")
        stage_results[stage] = {r["class"]: r for r in results.values() if r["mode"] == "empty"
                                and not any(f"{f}=" in r["overrides"] for f in LINK_FIELDS)}
        bisect_path = out / "bisect.json"
        stage_results[stage + "#bisect"] = json.loads(bisect_path.read_text()) if bisect_path.is_file() else {}
    counts = placement_counts()
    for c in classes:
        entry = {"class": c["class"], "creator": c["creator"], "area_like": c["area_like"], "archives": [], "results": {},
                 "placed_on_disc": counts.get(c["class"].lower(), 0)}
        for stage, scenario, _ in stages:
            r = stage_results[stage].get(c["class"])
            if not r:
                continue
            metrics = r.get("metrics") or {}
            if metrics.get("archives") not in (None, "-") and not entry["archives"]:
                entry["archives"] = metrics["archives"].split(",")
            if r.get("inferred_from"):
                res_inferred = r["inferred_from"]
            else:
                res_inferred = None
            res = {"status": "kills-player" if r["status"] == "crash" and not r.get("crash") and r.get("exit") == 1 else r["status"]}
            for key in ("phase", "note_batch"):
                if r.get(key):
                    res[key] = r[key]
            crash = r.get("crash") or (r.get("confirm") if isinstance(r.get("confirm"), dict) else {}).get("crash")
            if crash:
                res["crash"] = {k: crash[k] for k in ("signal", "panic", "top_frames") if crash.get(k)}
            for key in ("actors", "drawn", "alive", "hidden", "clipped", "move"):
                if key in metrics:
                    res[key] = int(metrics[key])
            if res_inferred:
                res["inferred_from"] = res_inferred
            res["row"] = metrics.get("row")
            res["nonresident"] = [] if metrics.get("nonresident") in (None, "-") else metrics["nonresident"].split(",")
            res["mount_ms"] = float(metrics["mount_ms"]) if "mount_ms" in metrics else None
            res["create_ms"] = float(metrics["create_ms"]) if "create_ms" in metrics else None
            if metrics.get("tpl_links") not in (None, "-"):
                res["template_links"] = parse_tpl_links(metrics["tpl_links"])
            verdict = stage_results[stage + "#bisect"].get(c["class"])
            if verdict:
                res["links"] = {k: v for k, v in verdict.items() if k != "tested"}
            entry["results"][stage] = res
        statuses = [r["status"] for r in entry["results"].values()]
        entry["summary"] = next((s for s in SEVERITY if s in statuses), "untested")
        entry["live_recreate"] = live_recreate_verdict(entry["results"])
        required = {}
        for r in entry["results"].values():
            if r.get("links", {}).get("result") == "required":
                required.update(r["links"]["fields"])
        entry["needs_links"] = required or None
        entries.append(entry)
    coverage = {s: {"recorded": sum(1 for e in entries if s in e["results"]), "total": len(entries),
                    "inferred": sum(1 for e in entries if e["results"].get(s, {}).get("inferred_from"))} for s, _, _ in stages}
    return {"generated": datetime.date.today().isoformat(), "game": "RMGE01 (US)", "driver": "ClassSweep.cpp",
            "partial": any(c["recorded"] < c["total"] for c in coverage.values()), "coverage": coverage,
            "order": "most placed on the disc first (placed_on_disc)",
            "stages": [{"stage": s, "scenario": n, "why": w} for s, n, w in stages], "link_fields": LINK_FIELDS,
            "live_recreate": {"safe": "recreate live", "suspect": "recreate live, tell the user it may not show or behave",
                              "unsafe": "do not recreate live: fall back to the reload path (pending)",
                              "unmeasured": "not measured yet: treat as unsafe until it is"},
            "statuses": {"ok": "created, alive and drawn after the observation window",
                         "ok-nomodel": "created and alive, has no model (logic actor)",
                         "nonactor": "created without any LiveActor (areas, managers, ...)",
                         "dead": "every actor it made was dead after the window (killed itself)",
                         "invisible": "alive with a model but not drawn (hidden, clipped or draw-invalid)",
                         "nan": "an actor's position became non-finite",
                         "crash": "native crash (signal)", "assert": "OSPanic or an OS fatal", "hang": "no progress for the stall timeout",
                         "oom": "native heap allocation failed", "exception": "uncaught C++ exception",
                         "kills-player": "Mario died while it ran"},
            "classes": entries}


ABS_PATH = re.compile(r"(?:/Users|/home|/private/var|/var/folders)/[^\s\"',;)\\]*")
SOURCE_ROOT = re.compile(r"(?:/Users|/home)/[^\s\"']*?/((?:src|include|native|docs)/)")
THREAD_ID = re.compile(r"\bthr_[a-z0-9]{6,}\b")


def scrub(value):
    """The report is committed to a public repo: absolute local paths become repo-relative (the game's __FILE__ in a panic
    message), any other absolute path becomes <path>, and BB thread ids go. Recurses through dicts and lists."""
    if isinstance(value, str):
        value = SOURCE_ROOT.sub(r"\1", value)
        value = ABS_PATH.sub("<path>", value)
        return THREAD_ID.sub("<thread>", value)
    if isinstance(value, list):
        return [scrub(v) for v in value]
    if isinstance(value, dict):
        return {scrub(k): scrub(v) for k, v in value.items()}
    return value


def cmd_report(args):
    stages = parse_stages(args.stages)
    data = scrub(build_catalog(args, stages))
    CATALOG.mkdir(parents=True, exist_ok=True)
    (CATALOG / "sweep.json").write_text(json.dumps(data, indent=1) + "\n")
    (CATALOG / "SWEEP.md").write_text(scrub(render_markdown(data)))
    print(f"wrote {CATALOG / 'sweep.json'} and SWEEP.md: {len(data['classes'])} classes")


def render_markdown(data):
    """The generated tables of SWEEP.md. Method and caveats are fixed text; numbers come from the data."""
    stages = [s["stage"] for s in data["stages"]]
    lines = ["# Class sweep (spike S2): results", "",
             f"Generated {data['generated']} by `native/tools/class_sweep.py report` from `sweep.json`. Do not edit the tables by hand.", "",
             SWEEP_METHOD, "", "## Results per stage", ""]
    if data["partial"]:
        lines[2:2] = ["**PARTIAL: the sweep is still running.** Classes are measured in order of how often the disc places them, so the "
                      "common ones come first; `untested` below are classes not run yet. Coverage: "
                      + "; ".join(f"{s} {c['recorded']}/{c['total']}" + (f" ({c['inferred']} inferred)" if c["inferred"] else "")
                                  for s, c in data["coverage"].items()) + ".", ""]
    order = SEVERITY + ["untested"]
    lines += ["| status | " + " | ".join(stages) + " | any stage (worst) |", "| --- |" + " --- |" * (len(stages) + 1)]
    for status in order:
        row = [sum(1 for c in data["classes"] if c["results"].get(s, {}).get("status") == status) for s in stages]
        worst = sum(1 for c in data["classes"] if c["summary"] == status)
        if any(row) or worst:
            lines.append(f"| {status} | " + " | ".join(map(str, row)) + f" | {worst} |")
    lines += ["", "## Failing classes", "",
              "Failures with empty links, worst stage first. `links` is the bisect verdict: the link fields that made the class work "
              "(value = the id used), `none` if nothing helped.", "",
              "| class | stage: status | where | links that fix it |", "| --- | --- | --- | --- |"]
    for c in data["classes"]:
        bad = {s: r for s, r in c["results"].items() if r["status"] in ("crash", "assert", "oom", "exception", "hang", "kills-player", "nan")}
        if not bad:
            continue
        status = "; ".join(f"{s}: {r['status']}" for s, r in bad.items())
        top = next((r["crash"]["top_frames"][0] for r in bad.values() if r.get("crash", {}).get("top_frames")), "")
        panic = next((r["crash"]["panic"] for r in bad.values() if r.get("crash", {}).get("panic")), "")
        where = (top or panic)[:70].replace("|", "/")
        links = []
        for s, r in bad.items():
            verdict = r.get("links")
            if verdict and verdict["result"] == "required":
                links.append(f"{s}: " + (", ".join(f"{k}={v}" for k, v in verdict["fields"].items()) or "none needed"))
            elif verdict:
                links.append(f"{s}: unresolved")
        lines.append(f"| {c['class']} | {status} | {where} | {'; '.join(links) or '-'} |")
    lines += ["", "## Classes that need archives mounted", "",
              f"{sum(1 for c in data['classes'] if any(r['nonresident'] for r in c['results'].values()))} classes had an archive that was "
              "not resident in the stage (listed per class in `sweep.json`: `archives`, per stage `nonresident`).", ""]
    return "\n".join(lines) + "\n"


SWEEP_METHOD = """## Method

- **Driver:** `src/Game/Scene/ClassSweep.cpp` (PETARI_NATIVE, off unless `PETARI_CLASS_SWEEP_JOBS` is set), on the editor runtime's hooks (registerRows, nativeLateConnect).
  For each class it mounts the game's archive list for the class (and waits), synthesizes a one-row ObjInfo from a row of the
  stage (the stage's own row of that class if any, else the first ObjInfo row renamed, args -1), sets every link field
  (CameraSetId, SW_*, MessageId, CastId, ViewGroupId, CommonPath_ID, ClippingGroupId, GroupId, DemoGroupId, MapParts_ID) to -1, creates
  the class 400 units in front of and 250 up from Mario, runs 24 frames, classifies, then kills what it made. Area-like creators get a 20-unit scale.
- **Harness:** `native/tools/class_sweep.py`. Batches of 25 classes per background-smoke process through `build/locked-app.sh`.
  A crash, assert or hang is attributed to the class that was in flight (log markers BEGIN/CREATED/RESULT/KILLED), the sweep
  resumes after it, and one in eight failures from a multi-class batch is re-run alone in a fresh process (a class that passes alone
  is recorded ok with a note; the rest keep the stack of the class in flight). Classes run in order of how often the disc places them
  (`placed_on_disc`). The resume point is `results.jsonl`. On the last stage, classes that failed on another stage are not run: their
  record is copied and marked `inferred_from`.
- **Bisect:** for failing classes, `keep` (the stage's own row's links) and then real ids harvested from the same fields of the stage's
  rows, reduced one field at a time while the class still passes.
- **Statuses:** see `statuses` in `sweep.json`. `invisible` and `dead` are heuristics from actor flags, not pixels.

## Caveats

- Classes are created once, in a stage that is already running. A pass says it did not crash in 24 frames, not that it behaves.
- Spawned objects with a template row keep the template's arguments; synthesized ones have all args -1 (defaults).
- Planet classes (PlanetMapCreator names, not in `cCreateTable`) are not covered.
- Areas and gravity are created tiny and left registered (no unregister), so later classes in a batch may be affected."""


# --- commands ----------------------------------------------------------------------------------------------

def parse_stages(text):
    if not text:
        return STAGES
    known = {s: (s, n, w) for s, n, w in STAGES}
    out = []
    for item in text.split(","):
        stage, _, scenario = item.partition(":")
        out.append((stage, int(scenario or 1), known.get(stage, (0, 0, "custom"))[2]))
    return out


def select_classes(args):
    classes = list_classes()
    if args.classes:
        wanted = [c.lower() for c in args.classes.split(",")]
        classes = [c for c in classes if c["class"].lower() in wanted]
    if args.slice:
        lo, _, hi = args.slice.partition(":")
        classes = classes[int(lo or 0):int(hi) if hi else None]
    return classes


def cmd_list(args):
    classes = select_classes(args)
    for c in classes:
        print(c["class"], "area" if c["area_like"] else "", c["creator"])
    print(len(classes), "classes", file=sys.stderr)


def cmd_run(args):
    jobs = make_jobs(order_by_placement(select_classes(args)))
    ok = True
    for stage, scenario, _ in parse_stages(args.stages):
        ok = run_stage(args, stage, scenario, jobs) and ok
    return 0 if ok else 1


def cmd_bisect(args):
    ok = True
    for stage, scenario, _ in parse_stages(args.stages):
        ok = bisect_stage(args, stage, scenario) and ok
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name, fn in (("list", cmd_list), ("run", cmd_run), ("bisect", cmd_bisect), ("report", cmd_report)):
        p = sub.add_parser(name)
        p.set_defaults(fn=fn)
        p.add_argument("--name", default="s2", help="sweep name: build/class-sweep/<name>")
        p.add_argument("--out-root", default=REPO / "build/class-sweep")
        p.add_argument("--stages", help="Stage:scenario,... (default: the three S2 stages)")
        p.add_argument("--classes", help="comma-separated class names (default: all of cCreateTable)")
        p.add_argument("--slice", help="lo:hi over the class list, for trial runs")
        p.add_argument("--app", help="Petari binary (default: this tree's build)")
        p.add_argument("--owner", default="class-sweep", help="locked-app.sh owner name")
        p.add_argument("--batch", type=int, default=20, help="classes per app process")
        p.add_argument("--observe", type=int, default=24, help="frames each class runs before the observation")
        p.add_argument("--frames", type=int, default=60000, help="PETARI_SMOKE_FRAMES (an upper bound)")
        p.add_argument("--timeout", type=int, default=2400, help="seconds per app process, counted inside the app lock")
        p.add_argument("--stall", type=int, default=240, help="seconds without log output, once the driver runs, before a hang")
        p.add_argument("--rerun", action="store_true", help="discard recorded results")
        p.add_argument("--keep-logs", action="store_true")
        p.add_argument("--confirm-every", type=int, default=8, help="re-run alone 1 in N failures from multi-class batches (1: all)")
        p.add_argument("--with-template-link", help="bisect: only failing classes whose template row has this link field set, e.g. CommonPath_ID")
        p.add_argument("--infer-known-bad", action="store_true",
                       help="do not run classes that failed on another stage; copy their result, marked inferred_from")
        p.add_argument("--normal-priority", action="store_true", help="do not run the app under taskpolicy -b")
        p.add_argument("--no-warm-seed", action="store_true", help="start every process cold (no shader seed)")
    args = parser.parse_args()
    sys.exit(args.fn(args) or 0)


if __name__ == "__main__":
    main()
