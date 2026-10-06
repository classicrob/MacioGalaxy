#!/bin/bash
# Removes per-run private cache dirs (a smoke's <run>-user/cache or <run>/user/cache, ~2 GB each)
# left behind by finished background runs, in the main checkout's build/ and every worktree's build/.
# Skips anything a running process has open. Logs, results, screenshots and saves are kept.
# usage: build/prune-run-artifacts-caches.sh [--dry-run] [age-minutes=30]
DRY=0; [ "$1" = --dry-run ] && { DRY=1; shift; }
AGE=${1:-30}
MAIN=$(cd "$(dirname "$0")/.." && pwd)
ROOTS=("$MAIN/build")
for w in "$MAIN"/../petari-worktrees/*/build; do [ -d "$w" ] && ROOTS+=("$(cd "$w" && pwd)"); done
# Worker thread storage (smoke runs often keep <run>-user dirs there too).
for t in "$HOME"/.bb/thread-storage/thr_*; do [ -d "$t" ] && ROOTS+=("$t"); done
OPEN=$(mktemp); lsof -Fn 2>/dev/null | sed -n 's/^n//p' > "$OPEN"
n=0
for r in "${ROOTS[@]}"; do
  while IFS= read -r d; do
    grep -qF "$d/" "$OPEN" && continue
    case "$d" in */shader-seed/*|*/game-data/*|*/macos-gx/*|*/saves/*) continue;; esac
    n=$((n+1)); [ $DRY = 0 ] && rm -rf "$d"
  done < <(find "$r" -mindepth 2 -maxdepth 7 -type d -name cache -path '*user*' -mmin +"$AGE" -prune 2>/dev/null)
done
rm -f "$OPEN"
echo "run caches removed: $n (dry-run=$DRY)"; df -h "$MAIN" | tail -1 | awk '{print "free: " $4}'
