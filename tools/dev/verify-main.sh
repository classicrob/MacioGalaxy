#!/bin/bash
# One-command check of a build tree: build every ctest executable that is missing or linked to a
# dylib that no longer exists, then run ctest and say which failures are dyld/stale and which are real.
#
# usage: build/verify-main.sh [--dry-run] [BUILD_DIR] [OWNER]
#   BUILD_DIR  default: <main checkout>/build/macos-gx (pass a worktree's build/macos-gx to check a worktree)
#   OWNER      lock owner for build/locked-build.sh, default "lead"
#   --dry-run  discover, check dylibs and print the plan; build and run nothing
#   --selftest run the built-in classifier/parser tests (no build tree needed)
#
# Why: several test executables that ctest runs are NOT in the default `all` target, so after a
# toolchain change (e.g. a Homebrew upgrade) ctest runs stale binaries ("Library not loaded") or
# reports "Not Run". Every target is derived from the executables `ctest --show-only=json-v1`
# (plus the CTestTestfile.cmake files, for executables that are not built yet) reports.
# All heavy work (cmake --build, ctest) goes through the MAIN checkout's build/locked-build.sh.
# Exit: 0 all passed, 1 test failures, 2 setup/build error.
set -u
SELF=$(python3 -I -c 'import os,sys;print(os.path.realpath(sys.argv[1]))' "$0")
HERE=$(cd "$(dirname "$0")" && pwd)
# Find the main checkout (its build/ holds the lock state): this script's dir if it is main's build/,
# else the git common dir's parent.
if [ -x "$HERE/locked-build.sh" ] && [ "$(basename "$HERE")" = build ]; then
  MAIN_BUILD=$HERE
else
  COMMON=$(git -C "$(dirname "$SELF")" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
  MAIN_BUILD=${COMMON%/.git}/build
fi
export VERIFY_MAIN_BUILD="${VERIFY_MAIN_BUILD:-$MAIN_BUILD}"
PYSRC=$(sed -n '/^#PY-BEGIN$/,$p' "$SELF" | sed '1d')
exec python3 -I -c "$PYSRC" "$@"
#PY-BEGIN
import json, os, re, subprocess, sys, shutil, time

OWNER_DEFAULT = "lead"
SYSTEM_PREFIXES = ("/usr/lib/", "/System/")

def sh(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **kw)

# ---------- discovery ----------
def parse_ctest_files(bd):
    """name -> argv, read from CTestTestfile.cmake files (needed when ctest omits `command`
    because the executable does not exist yet)."""
    out, seen, todo = {}, set(), [bd]
    while todo:
        d = todo.pop()
        f = os.path.join(d, "CTestTestfile.cmake")
        if d in seen or not os.path.isfile(f):
            continue
        seen.add(d)
        text = open(f, errors="replace").read()
        for m in re.finditer(r'^add_test\(\s*(?:"((?:[^"\\]|\\.)*)"|([^\s"()]+))((?:\s+"(?:[^"\\]|\\.)*")*)\s*\)', text, re.M):
            out[m.group(1) or m.group(2)] = [a.replace('\\"', '"') for a in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(3))]
        for m in re.finditer(r'^subdirs?\("([^"]+)"', text, re.M):
            todo.append(os.path.join(d, m.group(1)))
    return out

def list_targets(bd):
    # Fast path: the top-level Makefile has one `name: cmake_check_build_system` rule per real target
    # (`cmake --build --target help` lists 5000+ object files and takes ~20 s).
    mk = os.path.join(bd, "Makefile")
    if os.path.isfile(mk):
        t = set(re.findall(r'^([A-Za-z0-9_.+-]+): cmake_check_build_system$', open(mk, errors="replace").read(), re.M))
        if t:
            return t
    r = sh(["cmake", "--build", bd, "--target", "help"])
    t = set()
    for line in r.stdout.splitlines():
        m = re.match(r'^\.\.\. (\S+)', line) or re.match(r'^(\S+?):\s+\S+', line)
        if m:
            t.add(m.group(1))
    return t

def cache_value(bd, key):
    try:
        for line in open(os.path.join(bd, "CMakeCache.txt"), errors="replace"):
            if line.startswith(key + ":"):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None

def discover(bd):
    """-> (tests: {name: argv}, selected names (not labelled timing/disabled))"""
    r = subprocess.run(["ctest", "--test-dir", bd, "--show-only=json-v1"], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        raise SystemExit("verify-main: ctest --show-only failed in %s:\n%s" % (bd, r.stderr or r.stdout))
    j = json.loads(r.stdout)
    fallback = parse_ctest_files(bd)
    tests, selected = {}, []
    for t in j["tests"]:
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        argv = t.get("command") or fallback.get(t["name"], [])
        tests[t["name"]] = argv
        labels = props.get("LABELS", [])
        labels = [labels] if isinstance(labels, str) else labels
        if "timing" in labels or props.get("DISABLED") in (True, "true", "TRUE", "ON"):
            continue
        selected.append(t["name"])
    return tests, selected

def test_executables(bd, argvs, targets):
    """Executables inside bd named like a cmake target. -> ({exe path: target}, [unknown names])"""
    bdr = os.path.realpath(bd)
    exes, unknown = {}, set()
    for argv in argvs:
        for a in argv:
            if not os.path.isabs(a):
                continue
            d = os.path.dirname(a)
            if not (d == bd or d.startswith(bd + "/") or os.path.realpath(d).startswith(bdr + "/") or os.path.realpath(d) == bdr):
                continue
            base = os.path.basename(a)
            if base in targets:
                exes[a] = base
            elif "." not in base and not os.path.isdir(a) and re.search(r"(_|-)tests?$|^petari", base):
                unknown.add(base)
    return exes, sorted(unknown)

# ---------- dylib check ----------
_cache = {}
def _rpaths(path):
    r = sh(["otool", "-l", path]).stdout
    return re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (\S+)', r)

def missing_libs(path, depth=0):
    """Non-system dylibs (transitively) that a binary needs but that do not exist."""
    path = os.path.realpath(path)
    if path in _cache:
        return _cache[path]
    _cache[path] = []  # break cycles
    if depth > 6:
        return []
    r = sh(["otool", "-L", path])
    if r.returncode != 0:
        return []
    d = os.path.dirname(path)
    rps = None
    missing = []
    for line in r.stdout.splitlines()[1:]:
        m = re.match(r'^\s+(\S+) \(', line)
        if not m:
            continue
        dep = m.group(1)
        if dep.startswith(SYSTEM_PREFIXES):
            continue
        cands = []
        if dep.startswith("@rpath/"):
            if rps is None:
                rps = [x.replace("@loader_path", d).replace("@executable_path", d) for x in _rpaths(path)]
            cands = [os.path.join(x, dep[7:]) for x in rps]
        elif dep.startswith(("@loader_path/", "@executable_path/")):
            cands = [os.path.join(d, dep.split("/", 1)[1])]
        else:
            cands = [dep]
        found = next((c for c in cands if os.path.exists(c)), None)
        if found is None:
            missing.append(dep)
        else:
            for x in missing_libs(found, depth + 1):
                if x not in missing:
                    missing.append(x)
    _cache[path] = missing
    return missing

# ---------- ctest output ----------
HDR = re.compile(r'^\s*(\d+)/(\d+) Test\s+#(\d+): (\S+) \.+\s*(.*?)\s+[\d.]+ sec\s*$')

def parse_ctest_log(text):
    """-> (results {name: (status, output)}). status: Passed / Failed / Not Run / Timeout / Exception..."""
    res, cur, buf = {}, None, []
    def flush():
        if cur:
            res[cur[0]] = (cur[1], "\n".join(buf))
    for line in text.splitlines():
        m = HDR.match(line)
        if m:
            flush()
            cur, buf = (m.group(4), m.group(5).replace("*", "").strip()), []
        elif cur:
            if re.match(r'^\d+% tests passed', line):
                flush(); cur = None; buf = []
            else:
                buf.append(line)
    flush()
    return res

def classify(status, output):
    """-> 'dyld' (stale/toolchain), 'notrun' (stale/missing binary) or 'real'."""
    if re.search(r'Library not loaded|dyld\[|dyld:|Symbol not found|image not found', output):
        return "dyld"
    if status.startswith("Not Run"):
        return "notrun"
    return "real"

def selftest():
    fx = """\
    1/4 Test  #1: native_ok ........................   Passed    0.01 sec
    2/4 Test  #2: native_dyld ......................***Failed    0.02 sec
dyld[123]: Library not loaded: /opt/homebrew/opt/abseil/lib/libabsl_base.2501.0.0.dylib
  Reason: tried: '/opt/homebrew/opt/abseil/lib/libabsl_base.2501.0.0.dylib' (no such file)
    3/4 Test  #3: native_missing ...................***Not Run   0.00 sec
    4/4 Test  #4: native_real ......................***Failed    1.50 sec
assertion failed: x == 3

75% tests passed, 3 tests failed out of 4
"""
    r = parse_ctest_log(fx)
    assert set(r) == {"native_ok", "native_dyld", "native_missing", "native_real"}, r
    assert r["native_ok"][0] == "Passed"
    got = {n: classify(*r[n]) for n in r}
    assert got["native_dyld"] == "dyld" and got["native_missing"] == "notrun" and got["native_real"] == "real", got
    assert "assertion failed" in r["native_real"][1] and "75%" not in r["native_real"][1]
    # missing-lib detection on a real binary: system libs only -> nothing missing
    assert missing_libs(shutil.which("ls")) == []
    # vanished-dylib detection (direct and transitive) with a real, tiny toolchain build
    import tempfile
    cc = shutil.which("cc")
    if cc:
        with tempfile.TemporaryDirectory() as t:
            def w(n, c): open(os.path.join(t, n), "w").write(c)
            w("a.c", "int a(void){return 1;}"); w("b.c", "int a(void); int b(void){return a();}")
            w("m.c", "int b(void); int main(void){return b()*0;}")
            for cmd in ([cc, "-dynamiclib", "-o", t + "/liba.dylib", t + "/a.c", "-install_name", t + "/liba.dylib"],
                        [cc, "-dynamiclib", "-o", t + "/libb.dylib", t + "/b.c", t + "/liba.dylib", "-install_name", t + "/libb.dylib"],
                        [cc, "-o", t + "/m", t + "/m.c", t + "/libb.dylib"]):
                assert subprocess.run(cmd).returncode == 0, cmd
            _cache.clear(); assert missing_libs(t + "/m") == [], "healthy binary flagged"
            os.remove(t + "/liba.dylib")
            _cache.clear(); assert missing_libs(t + "/m") == [t + "/liba.dylib"], missing_libs(t + "/m")
    print("verify-main selftest: OK")

# ---------- main ----------
def main(argv):
    if "--selftest" in argv:
        return selftest()
    dry = "--dry-run" in argv
    pos = [a for a in argv if not a.startswith("--")]
    if any(a.startswith("--") and a not in ("--dry-run",) for a in argv) or len(pos) > 2:
        print(__doc__ if __doc__ else "usage: verify-main.sh [--dry-run] [BUILD_DIR] [OWNER]"); return 2
    main_build = os.environ["VERIFY_MAIN_BUILD"]
    bd = os.path.abspath(pos[0] if pos else os.path.join(main_build, "macos-gx"))
    owner = pos[1] if len(pos) > 1 else OWNER_DEFAULT
    locked = os.path.join(main_build, "locked-build.sh")
    if not os.path.isfile(os.path.join(bd, "CMakeCache.txt")):
        print("verify-main: %s is not a configured build dir (no CMakeCache.txt)" % bd); return 2
    if not os.access(locked, os.X_OK):
        print("verify-main: lock script not found: %s" % locked); return 2
    free = shutil.disk_usage("/").free / 2**30
    print("verify-main: build dir %s, owner %s, free disk %.0f GiB" % (bd, owner, free))
    if free < 40 and not dry:
        print("verify-main: refusing: less than 40 GiB free on /"); return 2

    locked_cmd = [locked, owner]
    src = cache_value(bd, "CMAKE_HOME_DIRECTORY")
    if dry:
        print("verify-main: (dry run skips the configure step, so tests added by a merge may be missing from this inventory)")
    elif src:
        # Re-run configure first so tests/targets added by a merge are in the inventory (and get built).
        clog = os.path.join(bd, "verify-main-configure.log")
        print("verify-main: configuring (cmake -S %s -B %s, log: %s) ..." % (src, bd, clog)); sys.stdout.flush()
        with open(clog, "w") as f:
            rc = subprocess.run(locked_cmd + ["cmake", "-S", src, "-B", bd], stdout=f, stderr=subprocess.STDOUT).returncode
        if rc != 0:
            print("\n".join(open(clog, errors="replace").read().splitlines()[-25:]))
            print("verify-main: configure FAILED (rc %d)" % rc); return 2
    else:
        print("verify-main: warning: no CMAKE_HOME_DIRECTORY in the cache; skipping configure")
    targets = list_targets(bd)
    if "petari" not in targets:
        print("verify-main: no target named petari in %s" % bd); return 2
    tests, selected = discover(bd)
    exes, unknown = test_executables(bd, [tests[n] for n in selected], targets)
    if unknown:
        print("verify-main: test executables with no cmake target (cannot build): " + ", ".join(unknown)); return 2
    missing = sorted(e for e in exes if not os.path.exists(e))
    broken = {}
    for e in sorted(exes):
        if os.path.exists(e):
            m = missing_libs(e)
            if m:
                broken[e] = m
    print("verify-main: %d tests selected (-LE timing), %d test executables: %d missing, %d linked to vanished dylibs"
          % (len(selected), len(exes), len(missing), len(broken)))
    for e, m in broken.items():
        print("  dyld-broken: %s -> %s" % (exes[e], ", ".join(sorted(set(os.path.basename(x) for x in m)))))
    if len(missing) > 8:
        print("  missing: " + ", ".join(exes[e] for e in missing[:8]) + ", ... (+%d more)" % (len(missing) - 8))
    else:
        for e in missing:
            print("  missing:     %s" % exes[e])
    names = ["petari"] + sorted(set(exes.values()))
    build_cmd = [locked, owner, "cmake", "--build", bd, "-j8", "--target"] + names
    ctest_cmd = [locked, owner, "ctest", "--test-dir", bd, "-j4", "--timeout", "600", "-LE", "timing", "--output-on-failure"]
    if dry:
        print("verify-main: DRY RUN. Would delete %d dyld-broken binaries to force a relink, then run:" % len(broken))
        print("  " + " ".join(build_cmd[:8] + ["<%d targets>" % len(names)]))
        print("  " + " ".join(ctest_cmd))
        return 0

    for e in broken:
        os.remove(e)  # make would consider it up to date; deleting forces the relink
    blog = os.path.join(bd, "verify-main-build.log")
    print("verify-main: building %d targets (log: %s) ..." % (len(names), blog)); sys.stdout.flush()
    t0 = time.time()
    with open(blog, "w") as f:
        rc = subprocess.run(build_cmd, stdout=f, stderr=subprocess.STDOUT).returncode
    print("verify-main: build %s in %.0f s" % ("ok" if rc == 0 else "FAILED (rc %d)" % rc, time.time() - t0))
    if rc != 0:
        tail = open(blog, errors="replace").read().splitlines()[-25:]
        print("\n".join(tail))
        if broken:
            print("verify-main: hint: a binary was linked to a vanished dylib; if the link line names an old "
                  "versioned path (e.g. Cellar/...), reconfigure the build dir (cmake -S . -B build/macos-gx).")
        return 2
    # Re-inventory: anything the build/configure added is now known; every selected test must have its binary.
    tests2, selected2 = discover(bd)
    exes2, unknown2 = test_executables(bd, [tests2[n] for n in selected2], list_targets(bd))
    late = sorted(set(exes2.values()) - set(exes.values()))
    if late:
        print("verify-main: %d test target(s) appeared after the build started: %s; building them" % (len(late), ", ".join(late)))
        rc = subprocess.run(locked_cmd + ["cmake", "--build", bd, "-j8", "--target"] + late,
                            stdout=open(blog, "a"), stderr=subprocess.STDOUT).returncode
        if rc != 0:
            print("\n".join(open(blog, errors="replace").read().splitlines()[-25:]))
            print("verify-main: build of late targets FAILED"); return 2
    selected, exes = selected2, exes2
    still = {}
    for e in exes:
        if os.path.exists(e):
            _cache.clear()
            m = missing_libs(e)
            if m:
                still[e] = m
    for e, m in still.items():
        print("  STILL dyld-broken after rebuild: %s -> %s (reconfigure or reinstall the dylib)" % (exes[e], ", ".join(m)))

    clog = os.path.join(bd, "verify-main-ctest.log")
    print("verify-main: running ctest (log: %s) ..." % clog); sys.stdout.flush()
    t0 = time.time()
    proc = subprocess.Popen(ctest_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    with open(clog, "w") as f:
        for line in proc.stdout:
            f.write(line)
            if HDR.match(line) and "Passed" not in line:
                print("  " + line.strip()); sys.stdout.flush()
    rc = proc.wait()
    res = parse_ctest_log(open(clog, errors="replace").read())
    passed = sorted(n for n, (s, _) in res.items() if s == "Passed")
    failed = {n: classify(s, o) for n, (s, o) in res.items() if s != "Passed"}
    expected = set(selected)
    never = sorted(expected - set(res))
    print("")
    print("== verify-main summary (%.0f s of ctest) ==" % (time.time() - t0))
    total = len(selected)
    if len(passed) + len(failed) + len(never) != total:  # ctest ran a different set than we inventoried
        total = len(passed) + len(failed) + len(never)
        print("note: ctest reported %d tests, inventory had %d" % (len(res), len(selected)))
    print("tests: %d passed, %d failed, %d no result, of %d" % (len(passed), len(failed), len(never), total))
    groups = {"dyld": "dyld/stale (rebuild/reconfigure)", "notrun": "stale: Not Run (binary missing)", "real": "REAL failures"}
    for k, title in groups.items():
        names_k = sorted(n for n, c in failed.items() if c == k)
        if names_k:
            print("%s: %s" % (title, ", ".join(names_k)))
    if never:
        print("no result recorded for: " + ", ".join(never) + " (ctest aborted?)")
    ok = rc == 0 and not failed and not never and not still
    print("RESULT: %s" % ("PASS %d/%d" % (len(passed), total) if ok else "FAIL"))
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
