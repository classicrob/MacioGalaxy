#!/bin/bash
# Tests for tools/dev/verify-main.sh and tools/dev/team-status.sh.
# usage: tools/dev/tests/test-verify-and-status.sh [BUILD_DIR]   (BUILD_DIR enables the verify-main --dry-run check)
set -u
DEV=$(cd "$(dirname "$0")/.." && pwd)
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
check() { # name, command...
  n=$1; shift; if "$@" >/dev/null 2>&1; then ok "$n"; else bad "$n"; fi
}

check "verify-main --selftest" "$DEV/verify-main.sh" --selftest

# --- team-status on a throwaway repo; a symlinked "Space" dir must never be entered ---
T=$(mktemp -d "${TMPDIR:-/tmp}/ts-test.XXXXXX"); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/main/build" "$T/space/deep" "$T/macio-worktrees"
touch "$T/space/deep/SENTINEL"
git -C "$T/main" init -q -b main && git -C "$T/main" -c user.name=t -c user.email=t@t commit -q --allow-empty -m init
git -C "$T/main" worktree add -q -b wt/a "$T/macio-worktrees/a"
git -C "$T/macio-worktrees/a" -c user.name=t -c user.email=t@t commit -q --allow-empty -m "a commit subject"
mkdir -p "$T/macio-worktrees/a/build/run1/cache" "$T/macio-worktrees/a/build/run2"
ln -s "$T/space" "$T/macio-worktrees/a/build/run2/cache"      # symlinked cache: must be skipped
ln -s "$T/space" "$T/main/build/perf-bench-warm"              # symlink into "Space": must be skipped
echo '#!/bin/bash
echo "== fake lock status"' > "$T/main/build/lock-status.sh"; chmod +x "$T/main/build/lock-status.sh"
OUT=$(TEAM_STATUS_MAIN="$T/main" TEAM_STATUS_WORKTREES="$T/macio-worktrees" TEAM_STATUS_SKIP_PS=1 "$DEV/team-status.sh" 2>&1)
echo "$OUT" | grep -q "^-- a .*ahead of main: 1"        && ok "worktree ahead count" || bad "worktree ahead count"
echo "$OUT" | grep -q "a commit subject"                && ok "ahead subject"        || bad "ahead subject"
echo "$OUT" | grep -q "fake lock status"                && ok "lock-status output"   || bad "lock-status output"
echo "$OUT" | grep -q "a/build/run1/cache"              && ok "worktree cache dir"   || bad "worktree cache dir"
echo "$OUT" | grep -q "perf-bench-warm: absent or a symlink" && ok "symlink skipped (main)" || bad "symlink skipped (main)"
echo "$OUT" | grep -q "run2/cache" && bad "symlinked wt cache listed" || ok "symlinked wt cache not followed"
echo "$OUT" | grep -q "SENTINEL" && bad "traversed symlink" || ok "no symlink traversal"
s=$(date +%s); "$DEV/team-status.sh" >/dev/null 2>&1; e=$(( $(date +%s) - s ))
[ "$e" -lt 5 ] && ok "real team-status took ${e}s (<5)" || bad "team-status too slow (${e}s)"

# --- verify-main dry run on a real configured build dir, if given ---
if [ -n "${1:-}" ]; then
  OUT=$("$DEV/verify-main.sh" --dry-run "$1" toolsmith 2>&1)
  echo "$OUT" | grep -q "DRY RUN" && echo "$OUT" | grep -q -- "-LE timing" && ok "verify-main --dry-run plan" || { bad "verify-main --dry-run plan"; echo "$OUT" | tail -5; }
fi
exit $fail
