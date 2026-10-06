#!/bin/bash
# Prove the committed state builds and passes on its own: fast-forward the permanent
# 'head-check' worktree to main's HEAD, build everything there and run ctest -LE timing.
# usage: build/head-check.sh      logs: build/head-check-<hash>-{build,test}.log
set -uo pipefail
MAIN=$(cd "$(dirname "$0")/.." && pwd -P)
WT=$(dirname "$MAIN")/petari-worktrees/head-check
[ -d "$WT" ] || { echo "head-check: worktree missing; run build/new-worktree.sh head-check" >&2; exit 2; }
# This checks MAIN: refuse to run while the worktree is on anything but its own branch (e.g. detached at level-editor).
[ "$(git -C "$WT" branch --show-current)" = "wt/head-check" ] || { echo "head-check: $WT is not on branch wt/head-check (it is on '$(git -C "$WT" branch --show-current)' / detached); switch it back first" >&2; exit 2; }
git -C "$WT" merge --ff-only main >/dev/null 2>&1 || { echo "head-check: cannot fast-forward $WT to main" >&2; exit 2; }
H=$(git -C "$WT" rev-parse --short HEAD)
echo "head-check: $H (clean: $(git -C "$WT" status --short | wc -l | tr -d ' ') dirty files)"
cd "$WT"
# A person may be playing from this worktree's app: never relink it under them.
APPT=petari
if pgrep -f "petari-worktrees/head-check/build/macos-gx/native/app/Petari.app" >/dev/null; then
  APPT=""; echo "head-check: Petari is running from head-check; NOT rebuilding the petari app target"
fi
B="$MAIN/build/head-check-$H-build.log"; T="$MAIN/build/head-check-$H-test.log"
"$MAIN/build/locked-build.sh" head-check bash -c 'cmake --build build/macos-gx -j6 && cmake --build build/macos-gx -j6 --target petari_tests '"$APPT" > "$B" 2>&1
BS=$?
echo "head-check: build exit $BS ($B)"
if [ $BS -ne 0 ]; then grep -E "error:|Error [0-9]|Undefined" "$B" | head -8; exit 1; fi
# EXCLUDE_FROM_ALL test executables that ctest registered (asset tests, when game data exists)
# are not built by 'all' or petari_tests: build the ones ctest cannot find.
MISSING=$(python3 - <<'PY'
import glob, os, re
seen = set()
for f in glob.glob("build/macos-gx/**/CTestTestfile.cmake", recursive=True):
    for m in re.finditer(r'add_test\(\[=\[[^\]]*\]=\]\s+"([^"]+)"', open(f).read()):
        exe = m.group(1)
        if exe.startswith("/") and not os.path.exists(exe) and os.path.basename(exe).startswith("petari_"):
            seen.add(os.path.basename(exe))
print(" ".join(sorted(seen)))
PY
)
if [ -n "$MISSING" ]; then
  echo "head-check: building ctest executables outside all: $MISSING"
  "$MAIN/build/locked-build.sh" head-check bash -c "cmake --build build/macos-gx -j6 --target $MISSING" >> "$B" 2>&1 || { echo "head-check: building those failed"; exit 1; }
fi
"$MAIN/build/locked-build.sh" head-check bash -c 'ctest --test-dir build/macos-gx -j4 --timeout 600 --output-on-failure -LE timing -E native_movie' > "$T" 2>&1
TS=$?
grep -E "tests passed|\(Failed\)|Subprocess" "$T" | head -12
echo "head-check: ctest exit $TS ($T)"
exit $TS
