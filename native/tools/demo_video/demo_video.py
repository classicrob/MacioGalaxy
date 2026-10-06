#!/usr/bin/env python3
"""Demo videos of player-input runs: key script -> background run -> per-frame dump -> input overlay -> H.264 -> contact sheet.

One command, from a level (stage + private save) and a key script to a delivery folder:

  demo_video.py make --slug orbit-run --title "Orbit Run" --app <Petari binary> --save <save dir> --stage <Galaxy> \\
                     --script keys.txt [--highlight 300-700] [--slow] [--crop x,y,w,h] [--owner <lane>]

writes ~/Petari-playtest/levels/<slug>/ (override with --out-root):
  <slug>.mp4            H.264 1280x720 60 fps, real time, with the overlay
  <slug>-highlight.mp4  0.5x cut of --highlight FIRST-LAST (script frames), if given
  <slug>-slow.mp4       the whole run at 0.5x, if --slow
  <slug>-inputs.csv     the raw per-frame input log: keys held, position, velocity, ground flag, camera position + axis
  <slug>-moves.log      the game's move log lines ([cappy]/[odyssey]/[input-script])
  <slug>-sheet.png      contact sheet (12 frames with times): look at it before you ship the video
  <slug>-keys.txt       the key script; <slug>-run.json  what ran (app, stage, frames, measured sync, ...)
and deletes the raw frame dump (about 11 MB a frame) and the run's save copy; --keep-run keeps them.

Pieces, each usable alone:  record (run + dump), render (overlay + encode + sheet from a run dir), sync (measure the
dump/input offset of a run dir), selftest (synthetic run through render; no game needed).

The input is real player input: PETARI_INPUT_SCRIPT types the keys through the native input layer (Input::keyEvent), the
path a person's keyboard takes, no warps or test-server pad. The script starts once play has been steady for 60 frames.
Key names and bindings: keys.py. The app must contain the dump mark (the probe marks "input-script" when the script
starts, so dump index N is script frame N + a small presentation latency).

SYNC. The overlay draws script frame f on dump frame f + latency. The tool measures that latency on every run: the first
frame where the logged state changes (position or camera) against the first dump frame whose image changes, and warns
when the latency it used is off by more than a frame. --latency auto (the default) uses the measured value.
"""
import argparse
import csv
import json
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import overlay  # noqa: E402
from keys import script_frames  # noqa: E402

FFMPEG = shutil.which("ffmpeg") or "/opt/homebrew/bin/ffmpeg"
FFPROBE = shutil.which("ffprobe") or "/opt/homebrew/bin/ffprobe"
MAIN = Path(os.environ.get("PETARI_MAIN", Path.home() / "petari"))
LEVELS = Path.home() / "Petari-playtest" / "levels"
FRAME_BYTES = 12 * 1024 * 1024  # a 2560x1440 PNG dump frame, with headroom
DEFAULT_LATENCY = 1  # measured on the shape-garden / le-main builds; the tool re-measures every run
MOVE = re.compile(r"^\[(input-script|cappy|odyssey)\] (on|start|done|stopped|throw|bounce|dive \(|cap bounce|.* landed|.*long jump)|"
                  r"^PETARI MODS: .* on$|^\[odyssey camera\] orbit on")


# ---------------------------------------------------------------- the test server's file protocol
def send(directory, command, timeout):
    name = f"{time.time_ns()}"
    tmp = Path(directory) / "cmd" / f"{name}.tmp"
    tmp.write_text(command + "\n")
    tmp.rename(tmp.with_suffix(".txt"))
    reply = Path(directory) / "reply" / f"{name}.txt"
    deadline = time.time() + timeout
    while time.time() < deadline:
        if reply.is_file():
            return reply.read_text().strip()
        time.sleep(0.05)
    return "error no reply"


# ---------------------------------------------------------------- record
def record(a, run):
    """Boot a private copy of a save into a stage, type the script, dump every script frame. Returns run.json's dict."""
    frames = script_frames(a.script)
    need = (frames + a.tail) * FRAME_BYTES * (0 if a.no_dump else 1)
    free = shutil.disk_usage(run.parent).free
    if need + 10 * 2**30 > free:
        sys.exit(f"demo_video: {frames + a.tail} dumped frames need about {need / 2**30:.0f} GiB; only {free / 2**30:.0f} GiB free "
                 f"(keeping 10 GiB). Shorten the script or free disk.")
    (run / "server/cmd").mkdir(parents=True)
    (run / "server/reply").mkdir()
    subprocess.run(["cp", "-cR", str(Path(a.save).resolve()), str(run / "user")], check=True)
    shutil.rmtree(run / "user/cache", ignore_errors=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("PETARI_")}
    # PETARI_SMOKE is empty on purpose: a no-op for the app, but locked-app.sh treats an app run without PETARI_SMOKE= /
    # --test-fixture as a person playing and collapses the lock slots to one.
    env.update(PETARI_SMOKE="", PETARI_SMOKE_BACKGROUND="1", PETARI_OPEN_STAGE=a.stage, PETARI_OPEN_FILE="1", PETARI_OPEN_EDIT="0",
               PETARI_STAGE_GATE_MS="0", PETARI_SMOKE_FRAMES="10000000", PETARI_TEST_SERVER=str(run / "server"), PETARI_FAST_BOOT="1",
               PETARI_PIPELINE_GLOBAL_PRECOMPILE="0", SDL_AUDIODRIVER="dummy", PETARI_LOCK_CLASS="functional", PETARI_LOCK_TIMEOUT="5400",
               PETARI_CAPPY_TRACE="1", PETARI_INPUT_SCRIPT=str(Path(a.script).resolve()), PETARI_INPUT_SCRIPT_LOG=str(run / "input.csv"))
    if not a.no_dump:
        env.update(PETARI_SURFACE_DUMP=str(run / "frames"), PETARI_SURFACE_DUMP_LABELS="input-script", PETARI_SURFACE_DUMP_EVERY="1",
                   PETARI_SURFACE_DUMP_SPAN=str(frames + a.tail))
    for kv in a.env:
        k, v = kv.split("=", 1)
        env[k] = v
    cmd = [str(Path(a.lock)), a.owner, str(Path(a.app).resolve()), "--disc", str(a.disc), "--user", str(run / "user")]
    log = (run / "app.log").open("wb")
    t0 = time.time()
    # Own process group: the only thing this tool ever signals is the group it started (never a name or path pattern).
    proc = subprocess.Popen(cmd, cwd=str(MAIN), env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    print(f"[demo_video] pid {proc.pid}: {frames} script frames (+{a.tail} tail) on {a.stage}", flush=True)
    status, ok = "timeout", False
    try:
        while time.time() - t0 < a.timeout:
            if proc.poll() is not None:
                status = f"app exited {proc.returncode}"
                break
            text = (run / "app.log").read_bytes().decode(errors="replace")
            if "[input-script] done" in text or "[input-script] stopped" in text:
                time.sleep(max(1.0, a.tail / 30))  # let the tail frames be dumped
                ok = "[input-script] done" in text
                status = "done" if ok else "stopped"
                break
            time.sleep(1)
    finally:
        if proc.poll() is None:
            send(run / "server", "quit", 15)
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, 15)
                proc.wait(timeout=10)
        shutil.rmtree(run / "user/cache", ignore_errors=True)
    text = (run / "app.log").read_bytes().decode(errors="replace")
    seeded = []
    if a.seed_caches not in ("", "none") and "[input-script] done" in text:
        seeded = save_caches(run / "user", Path(a.save) if a.seed_caches == "save" else Path(a.seed_caches))
    moves = [l for l in text.splitlines() if MOVE.match(l)]
    (run / "moves.log").write_text("\n".join(moves) + "\n")
    dumped = len(list((run / "frames").glob("*.png"))) if (run / "frames").exists() else 0
    info = dict(status=status, ok=ok, script=str(a.script), frames=frames, dumped=dumped, seconds=round(time.time() - t0),
                app=str(a.app), stage=a.stage, exit=proc.returncode)
    if seeded:
        info["seeded_caches"] = seeded
    if ok and not a.no_dump and dumped == 0:
        info["ok"] = False
        info["status"] = "no frames dumped: this app has no dump mark (PETARI_SURFACE_DUMP_LABELS=input-script); use a build with it"
    return info


def save_caches(user, target):
    """Back the run's warm shader/pipeline caches (SQLite, consistent copies) into a reusable seed dir, so the next run
    starts warm instead of recooking. Only after a clean finish; the app has exited, so the files are quiescent."""
    import sqlite3
    done = []
    for name in ("pipeline_cache.db", "dawn_cache.db"):
        src = user / name
        if not src.is_file():
            continue
        target.mkdir(parents=True, exist_ok=True)
        tmp = target / (name + ".new")
        a = sqlite3.connect(f"file:{src}?mode=ro", uri=True)
        b = sqlite3.connect(tmp)
        with b:
            a.backup(b)
        a.close()
        b.close()
        for stale in (target / (name + "-wal"), target / (name + "-shm")):
            stale.unlink(missing_ok=True)
        os.replace(tmp, target / name)
        done.append(name)
    return done


# ---------------------------------------------------------------- sync
FIELDS = ("x", "y", "z", "cam_x", "cam_y", "cam_z")


def read_rows(run):
    return list(csv.DictReader(open(run / "input.csv")))


def first_state_change(rows, eps=0.5):
    """First script frame whose logged position or camera differs from frame 0's: when something visible starts."""
    base = [float(rows[0][k]) for k in FIELDS]
    for r in rows:
        if any(abs(float(r[k]) - b) > eps for k, b in zip(FIELDS, base)):
            return int(r["frame"])
    return None


def small_gray(path):
    from PIL import Image
    return list(Image.open(path).convert("L").resize((160, 90)).getdata())


def measure_latency(run, rows, search=40):
    """Dump-frame offset of the script: (first image change) - (first logged state change), or None if nothing moves."""
    c = first_state_change(rows)
    if c is None or c < 2:
        return None, c
    frames = run / "frames"
    last = c + search
    base = small_gray(frames / "surface-input-script-0.png")
    diffs = []
    for i in range(0, last):
        p = frames / f"surface-input-script-{i}.png"
        if not p.exists():
            break
        g = small_gray(p)
        diffs.append(sum(abs(x - y) for x, y in zip(g, base)) / len(g))
    quiet = diffs[:max(1, c - 1)]  # frames before anything moves: the noise floor (idle animation, fades)
    threshold = max(0.12, max(quiet) * 3)  # a character moving alone is ~0.2 on this scale; a camera move is far more
    for i, v in enumerate(diffs):
        if i >= 1 and v > threshold and (i >= c - 1):
            return i - c, c
    return None, c


# ---------------------------------------------------------------- render
_STATE = {}


def _init(all_frames, title, frames_dir, latency, crop, layout):
    _STATE.update(all=all_frames, title=title, dir=frames_dir, latency=latency, crop=crop, layout=layout,
                  used=overlay.used_keys(all_frames))


def _frame(f):
    from PIL import Image
    s = _STATE
    src = Path(s["dir"]) / f"surface-input-script-{f + s['latency']}.png"
    img = Image.open(src).convert("RGB")
    if s["crop"]:
        x, y, w, h = s["crop"]
        img = img.crop((x, y, x + w, y + h))
    if s["layout"] == "band":
        # The scene keeps its aspect in the area above the overlay band; nothing is drawn over it.
        room = 720 - overlay.reserved_height(s["used"])
        w = min(1280, round(room * img.width / img.height))
        scene = img.resize((w, room), Image.LANCZOS)
        img = Image.new("RGB", (1280, 720), (0, 0, 0))
        img.paste(scene, ((1280 - w) // 2, 0))
        overlay.draw(img, f, s["all"][f], s["all"], s["title"], s["used"], band=True)
    else:
        img = img.resize((1280, 720), Image.LANCZOS)
        overlay.draw(img, f, s["all"][f], s["all"], s["title"], s["used"])
    return f, img.tobytes()


def parse_range(text):
    a, b = text.split("-")
    return int(a), int(b)


def ffmpeg_cmd(outs):
    """One ffmpeg reading raw RGB frames on stdin and writing every requested cut: [(path, trim_or_None, slow_factor)]."""
    n = len(outs)
    graph = [f"[0:v]split={n}" + "".join(f"[s{i}]" for i in range(n))]
    cmd = [FFMPEG, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "1280x720", "-framerate", "60", "-i", "-"]
    for i, (_, trim, slow) in enumerate(outs):
        chain = f"[s{i}]"
        if trim:
            chain += f"trim=start_frame={trim[0]}:end_frame={trim[1] + 1},setpts=PTS-STARTPTS,"
        if slow != 1:
            chain += f"setpts={slow}*PTS,"
        graph.append(chain + f"format=yuv420p[v{i}]")
    cmd += ["-filter_complex", ";".join(graph)]
    for i, (path, _, _) in enumerate(outs):
        cmd += ["-map", f"[v{i}]", "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-r", "60", "-movflags", "+faststart", str(path)]
    return cmd


def render(run, slug, a, dest):
    """Overlay + encode + sheet from a run dir (input.csv + frames/). Returns the info dict."""
    rows = read_rows(run)
    all_frames = [set() if r["keys"] == "-" else set(r["keys"].split("+")) for r in rows]
    latency, change = (None, None)
    if (run / "frames" / "surface-input-script-0.png").exists():
        latency, change = measure_latency(run, rows)
    wanted = a.latency
    if wanted == "auto":
        used = latency if latency is not None and 0 <= latency <= 6 else DEFAULT_LATENCY
    else:
        used = int(wanted)
    warn = None
    if latency is not None and abs(latency - used) > 1:
        warn = f"measured latency {latency} but drawing with {used}: the overlay may be off by {abs(latency - used)} frames"
        print("[demo_video] WARNING:", warn, flush=True)
    print(f"[demo_video] sync: first state change at script frame {change}, measured latency {latency}, using {used}", flush=True)

    dest.mkdir(parents=True, exist_ok=True)
    outs = [(dest / f"{slug}.mp4", None, 1)]
    if a.highlight:
        first, last = parse_range(a.highlight)
        outs.append((dest / f"{slug}-highlight.mp4", (first, last), 2))
    if a.slow:
        outs.append((dest / f"{slug}-slow.mp4", None, 2))
    usable = [f for f in range(len(rows)) if (run / "frames" / f"surface-input-script-{f + used}.png").exists()]
    if not usable:
        sys.exit("demo_video: no dumped frames to render")
    count = usable[-1] + 1
    crop = tuple(int(v) for v in a.crop.split(",")) if a.crop else None
    ff = subprocess.Popen(ffmpeg_cmd(outs), stdin=subprocess.PIPE)
    done = 0
    with multiprocessing.Pool(max(1, (os.cpu_count() or 2) - 1), initializer=_init,
                              initargs=(all_frames, a.title, str(run / "frames"), used, crop, a.layout)) as pool:
        for f, raw in pool.imap(_frame, range(count), chunksize=2):
            ff.stdin.write(raw)
            done += 1
            if not a.keep_run:  # peak disk falls as the dump is consumed
                try:
                    (run / "frames" / f"surface-input-script-{f + used}.png").unlink()
                except FileNotFoundError:
                    pass
    ff.stdin.close()
    if not a.keep_run:
        shutil.rmtree(run / "frames", ignore_errors=True)  # the tail frames too
    if ff.wait() != 0:
        sys.exit("demo_video: ffmpeg failed")
    print(f"[demo_video] {done} frames drawn and encoded", flush=True)

    shutil.copyfile(run / "input.csv", dest / f"{slug}-inputs.csv")
    for name, target in (("moves.log", f"{slug}-moves.log"),):
        if (run / name).exists():
            shutil.copyfile(run / name, dest / target)
    sheet = dest / f"{slug}-sheet.png"
    contact_sheet(outs[0][0], sheet)
    return dict(frames_drawn=done, latency_measured=latency, latency_used=used, first_state_change=change, warning=warn,
                outputs=[str(p) for p, _, _ in outs] + [str(sheet)])


def probe(path):
    out = subprocess.run([FFPROBE, "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=codec_name,width,height,pix_fmt,r_frame_rate,nb_frames", "-show_entries", "format=duration",
                          "-of", "json", str(path)], capture_output=True, text=True).stdout
    j = json.loads(out)
    s = j["streams"][0]
    return dict(codec=s["codec_name"], width=s["width"], height=s["height"], pix_fmt=s["pix_fmt"], fps=s["r_frame_rate"],
                frames=int(s.get("nb_frames", 0)), seconds=float(j["format"]["duration"]))


def contact_sheet(video, sheet, count=12, cols=4):
    from PIL import Image, ImageDraw
    import tempfile
    dur = probe(video)["seconds"]
    tiles = []
    with tempfile.TemporaryDirectory() as tmp:
        for i in range(count):
            t = dur * (i + 0.5) / count
            p = Path(tmp) / f"{i}.png"
            subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-ss", f"{t:.3f}", "-i", str(video), "-frames:v", "1", str(p)], check=True)
            im = Image.open(p).convert("RGB").resize((640, 360))
            ImageDraw.Draw(im).text((560, 340), f"{t:5.2f}s", fill=(255, 0, 255))
            tiles.append(im)
    rows = (len(tiles) + cols - 1) // cols
    out = Image.new("RGB", (640 * cols, 360 * rows))
    for k, im in enumerate(tiles):
        out.paste(im, ((k % cols) * 640, (k // cols) * 360))
    out.save(sheet)


def cleanup(run, keep):
    if keep:
        return
    for d in ("frames", "overlay", "user", "server"):
        shutil.rmtree(run / d, ignore_errors=True)


# ---------------------------------------------------------------- selftest (no game)
def selftest(_args):
    """A synthetic run through sync + render: a square moves on a dark ground, the log says when it starts moving."""
    import tempfile
    from PIL import Image, ImageDraw
    from keys import Plan
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        run = tmp / "run"
        (run / "frames").mkdir(parents=True)
        plan = Plan()
        plan.wait(20)
        plan.hold(30, "W")
        plan.hold(10, "W", "Space")
        plan.overlay("L", start=25, frames=20)
        plan.hold(10)
        n = plan.write(tmp / "keys.txt")
        assert n == script_frames(tmp / "keys.txt") == 70
        latency = 2
        start = 20  # the logged state starts changing on script frame 20
        with open(run / "input.csv", "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["frame", "line", "keys", "x", "y", "z", "vx", "vy", "vz", "ground", "cam_x", "cam_y", "cam_z", "cam_fx", "cam_fy", "cam_fz"])
            for f, k in enumerate(plan.frames):
                x = max(0, f - start + 1) * 5.0
                w.writerow([f, 0, "+".join(sorted(k)) or "-", x, 0, 0, 0, 0, 0, 1, x, 5, 0, 1, 0, 0])
        for i in range(n + 6):  # dump frame i shows script frame i - latency
            img = Image.new("RGB", (2560, 1440), (20, 30, 40))
            ImageDraw.Draw(img).rectangle([200 + 40 * max(0, i - latency - start + 1), 500, 500 + 40 * max(0, i - latency - start + 1), 800],
                                          fill=(230, 80, 60))
            img.save(run / "frames" / f"surface-input-script-{i}.png")
        (run / "moves.log").write_text("[input-script] start\n[input-script] done after 70 frames\n")
        shutil.copytree(run, tmp / "run_src")
        measured, change = measure_latency(run, read_rows(run))
        assert change == start and measured == latency, (measured, change)
        a = argparse.Namespace(latency="auto", highlight="10-40", slow=True, crop="", title="Selftest", keep_run=False, layout="band")
        dest = tmp / "out"
        info = render(run, "selftest", a, dest)
        assert info["latency_used"] == latency and info["warning"] is None, info
        main = probe(dest / "selftest.mp4")
        assert (main["codec"], main["width"], main["height"], main["pix_fmt"]) == ("h264", 1280, 720, "yuv420p"), main
        assert main["fps"] == "60/1" and abs(main["seconds"] - n / 60) < 0.05, main
        slow = probe(dest / "selftest-slow.mp4")
        assert abs(slow["seconds"] - 2 * n / 60) < 0.1, slow
        hl = probe(dest / "selftest-highlight.mp4")
        assert abs(hl["seconds"] - 2 * 31 / 60) < 0.1, hl
        assert (dest / "selftest-sheet.png").stat().st_size > 10000
        assert (dest / "selftest-inputs.csv").exists() and (dest / "selftest-moves.log").exists()
        assert not list((run / "frames").glob("*.png")), "raw frames must be deleted as they are consumed"
        # band layout: nothing is drawn above the reserved band, whatever keys are held
        canvas = Image.new("RGB", (1280, 720), (0, 0, 0))
        overlay.draw(canvas, 35, plan.frames[35], plan.frames, "Selftest", overlay.used_keys(plan.frames), band=True)
        scene_rows = 720 - overlay.reserved_height(overlay.used_keys(plan.frames))
        assert canvas.crop((0, 0, 1280, scene_rows)).getbbox() is None, "the band layout drew over the scene area"
        assert canvas.crop((0, scene_rows, 1280, 720)).getbbox() is not None
        # a second run drawn in the overlay layout, with a deliberately wrong forced latency: it must warn
        run2 = tmp / "run2"
        shutil.copytree(tmp / "run_src", run2)
        a2 = argparse.Namespace(latency="5", highlight="", slow=False, crop="", title="", keep_run=False, layout="overlay")
        info2 = render(run2, "selftest2", a2, tmp / "out2")
        assert info2["warning"] and info2["latency_measured"] == latency, info2
        assert probe(tmp / "out2" / "selftest2.mp4")["height"] == 720
        # warm caches are copied consistently into the seed dir
        import sqlite3
        (tmp / "user").mkdir()
        db = sqlite3.connect(tmp / "user" / "pipeline_cache.db")
        db.execute("create table x(a)")
        db.execute("insert into x values (7)")
        db.commit()
        db.close()
        assert save_caches(tmp / "user", tmp / "seed") == ["pipeline_cache.db"]
        assert sqlite3.connect(tmp / "seed" / "pipeline_cache.db").execute("select a from x").fetchone() == (7,)
        print("selftest PASS:", json.dumps(dict(main=main, slow=slow["seconds"], highlight=hl["seconds"], latency=measured)))
    return 0


# ---------------------------------------------------------------- CLI
def add_run_args(p):
    p.add_argument("--app", required=True, help="Petari binary (a build with PETARI_INPUT_SCRIPT and the dump mark)")
    p.add_argument("--save", required=True, help="a private COPY of a save dir (mods.txt, camera.txt, patches); cloned per run")
    p.add_argument("--stage", required=True, help="galaxy to boot into (mission 1 of file 1)")
    p.add_argument("--script", required=True, help="key script: lines of '<frames> [key ...]'")
    p.add_argument("--owner", default="video", help="build/locked-app.sh owner name (your lane)")
    p.add_argument("--lock", default=str(MAIN / "build/locked-app.sh"))
    p.add_argument("--disc", default=str(MAIN / "build/game-data/RMGE01"))
    p.add_argument("--seed-caches", nargs="?", const="save", default="save", metavar="DIR",
                   help="after a clean run, copy the warm pipeline_cache.db / dawn_cache.db into DIR (default: back into --save, "
                        "which must be a private copy) so the next run starts warm; 'none' disables")
    p.add_argument("--tail", type=int, default=60, help="frames dumped after the script ends")
    p.add_argument("--no-dump", action="store_true", help="rehearsal: run the script and log, no frames (check moves.log / inputs.csv)")
    p.add_argument("--env", action="append", default=[], help="extra K=V for the app")
    p.add_argument("--timeout", type=int, default=1800)


def add_render_args(p):
    p.add_argument("--title", default="")
    p.add_argument("--latency", default="auto", help="dump frames between a script frame and its picture; 'auto' measures it")
    p.add_argument("--layout", choices=("band", "overlay"), default="band",
                   help="band: the scene is scaled into the area above the input band, nothing covers it (default); "
                        "overlay: translucent strip + timeline over the bottom of the scene (the first orbit videos)")
    p.add_argument("--crop", default="", help="x,y,w,h in 2560x1440 dump pixels, to keep the character framed")
    p.add_argument("--highlight", default="", help="FIRST-LAST script frames for a 0.5x highlight cut")
    p.add_argument("--slow", action="store_true", help="also write the whole run at 0.5x")
    p.add_argument("--keep-run", action="store_true", help="keep the raw frame dump and the save copy (disk: ~11 MB a frame)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make", help="record + render into the delivery folder")
    m.add_argument("--slug", required=True)
    m.add_argument("--out-root", default=str(LEVELS))
    m.add_argument("--work", default="", help="scratch dir for the run (default: <out>/.run, removed afterwards)")
    add_run_args(m)
    add_render_args(m)
    r = sub.add_parser("record", help="run + dump only, into --out (a new dir)")
    r.add_argument("--out", required=True)
    add_run_args(r)
    d = sub.add_parser("render", help="overlay + encode + sheet from a run dir")
    d.add_argument("run")
    d.add_argument("--slug", required=True)
    d.add_argument("--out-root", default=str(LEVELS))
    add_render_args(d)
    s = sub.add_parser("sync", help="measure the dump/input latency of a run dir")
    s.add_argument("run")
    sub.add_parser("selftest", help="synthetic run through sync + render (needs ffmpeg; no game)")
    a = ap.parse_args()

    if a.cmd == "selftest":
        sys.exit(selftest(a))
    if a.cmd == "sync":
        run = Path(a.run)
        lat, change = measure_latency(run, read_rows(run))
        print(json.dumps(dict(first_state_change=change, latency=lat)))
        return
    if a.cmd == "render":
        run = Path(a.run).resolve()
        dest = Path(a.out_root) / a.slug
        info = render(run, a.slug, a, dest)
        cleanup(run, a.keep_run)
        print(json.dumps(info, indent=1))
        return
    if a.cmd == "record":
        run = Path(a.out).resolve()
        if run.exists():
            sys.exit(f"{run} exists; pick a new --out")
        info = record(a, run)
        (run / "run.json").write_text(json.dumps(info, indent=1))
        print(json.dumps(info))
        sys.exit(0 if info["ok"] else 1)
    # make
    dest = Path(a.out_root) / a.slug
    run = Path(a.work).resolve() if a.work else dest / ".run"
    if run.exists():
        sys.exit(f"{run} exists; remove it or pass another --work")
    dest.mkdir(parents=True, exist_ok=True)
    run.mkdir()
    info = record(a, run)
    (run / "run.json").write_text(json.dumps(info, indent=1))
    print("\n".join((run / "moves.log").read_text().splitlines()))
    if not info["ok"]:
        print(json.dumps(info))
        print(f"[demo_video] run did not finish cleanly ({info['status']}); keeping {run} for inspection")
        sys.exit(1)
    if a.no_dump:
        shutil.copyfile(run / "input.csv", dest / f"{a.slug}-inputs.csv")
        shutil.copyfile(run / "moves.log", dest / f"{a.slug}-moves.log")
        cleanup(run, a.keep_run)
        print(f"[demo_video] rehearsal: inputs and moves written to {dest}")
        return
    result = render(run, a.slug, a, dest)
    shutil.copyfile(a.script, dest / f"{a.slug}-keys.txt")
    info.update(result)
    info["video"] = probe(dest / f"{a.slug}.mp4")
    (dest / f"{a.slug}-run.json").write_text(json.dumps(info, indent=1))
    cleanup(run, a.keep_run)
    if not a.keep_run:
        shutil.rmtree(run, ignore_errors=True)
    print(json.dumps(info, indent=1))


if __name__ == "__main__":
    main()
