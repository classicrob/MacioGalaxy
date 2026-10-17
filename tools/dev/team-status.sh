#!/bin/bash
# Fast (<5 s), read-only snapshot of the agent team's machine state, for the lead.
#   disk free vs the 40 GB floor | worktrees (size, commits ahead of main, dirty files) | locks |
#   running Petari apps (owner inferred from path) | MTLCompilerService CPU | big cache dirs
# usage: build/team-status.sh        (works from anywhere; only reads)
# Env overrides (used by the test): TEAM_STATUS_MAIN, TEAM_STATUS_WORKTREES, TEAM_STATUS_SKIP_PS=1
# Never traverses ~/Space: no recursive walk of main's build/ (some subdirs are symlinks into
# ~/Space), find -P with depth limits, and symlinked dirs are skipped, never followed.
export LC_ALL=C
SELF=$(python3 -I -c 'import os,sys;print(os.path.realpath(sys.argv[1]))' "$0")
if [ -z "${TEAM_STATUS_MAIN:-}" ]; then
  COMMON=$(git -C "$(dirname "$SELF")" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
  TEAM_STATUS_MAIN=${COMMON%/.git}
fi
MAIN=$TEAM_STATUS_MAIN
WTROOT=${TEAM_STATUS_WORKTREES:-$(dirname "$MAIN")/macio-worktrees}
FLOOR=40 WARN=45
T0=$(date +%s)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/team-status.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT

# --- disk ---
avail_kb=$(df -k / | awk 'NR==2{print $4}')
avail_gb=$(awk -v k="$avail_kb" 'BEGIN{printf "%.1f", k/1048576}')
state=OK
awk -v a="$avail_gb" -v w=$WARN 'BEGIN{exit !(a<w)}' && state="WARNING: below ${WARN} GB"
awk -v a="$avail_gb" -v f=$FLOOR 'BEGIN{exit !(a<f)}' && state="CRITICAL: BELOW THE ${FLOOR} GB FLOOR"
echo "== disk: ${avail_gb} GB free on / (floor ${FLOOR} GB) [$state]"

# --- worktrees (gathered in parallel, printed in order) ---
human_size() { # path -> du -sh; never follows symlinks
  du -sh -P "$1" 2>/dev/null | awk '{print $1}'
}
wt_report() {
  wt=$1; n=$(basename "$wt"); out=$TMP/wt.$n
  {
    br=$(git -C "$wt" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?")
    dirty=$(git -C "$wt" status --porcelain --untracked-files=no 2>/dev/null | wc -l | tr -d ' ')
    ahead=$(git -C "$wt" rev-list --count main..HEAD 2>/dev/null || echo "?")
    echo "-- $n  [$br]  size $(human_size "$wt")  dirty tracked files: $dirty  ahead of main: $ahead"
    git -C "$wt" log --format='     %h %s' main..HEAD 2>/dev/null | head -5 | cut -c1-150
    [ "${ahead:-0}" -gt 5 ] 2>/dev/null && echo "     ... and $((ahead - 5)) more"
  } > "$out"
}
echo "== worktrees under $WTROOT"
n_wt=0
for wt in "$WTROOT"/*/; do
  wt=${wt%/}
  [ -d "$wt" ] && [ ! -L "$wt" ] && [ -e "$wt/.git" ] || continue
  n_wt=$((n_wt + 1))
  wt_report "$wt" &
done
wait
if [ $n_wt = 0 ]; then echo "   (none)"; else for f in "$TMP"/wt.*; do [ -e "$f" ] && cat "$f"; done; fi

# --- locks ---
echo "== locks (build/lock-status.sh)"
if [ -x "$MAIN/build/lock-status.sh" ]; then (cd "$MAIN" && build/lock-status.sh 2>&1); else echo "   lock-status.sh not found in $MAIN/build"; fi

# --- processes ---
if [ -z "${TEAM_STATUS_SKIP_PS:-}" ]; then
  PS=$(ps -axo pid=,etime=,%cpu=,command= 2>/dev/null)
else PS=""; fi
echo "== running Petari apps"
printf '%s\n' "$PS" | awk -v main="$MAIN" -v root="$WTROOT" '
  { pid=$1; et=$2; cpu=$3; exe=$4
    if (exe !~ /\/Petari\.app\/Contents\/MacOS\/Petari$/ && exe !~ /\/Petari$/) next
    if (exe !~ /^\//) next
    cmd=$0; user=""
    if (match(cmd, /--user [^ ]+/)) user=substr(cmd, RSTART+7, RLENGTH-7)
    own=owner(exe)
    uo=(user!="")?owner(user):""
    printf "   pid %s  up %s  cpu %s%%  owner %s%s\n      %s\n", pid, et, cpu, own, (uo!="" && uo!=own)?"  (user dir: " uo ")":"", exe
    n++ }
  function owner(p,   rest, a) {
    if (index(p, root "/")==1) { rest=substr(p, length(root)+2); split(rest, a, "/"); return a[1] }
    if (index(p, main "/")==1) { rest=substr(p, length(main)+2); split(rest, a, "/"); return "main-checkout:" a[1] "/" a[2] }
    return "other"
  }
  END { if (!n) print "   (none)" }'
echo "== MTLCompilerService"
printf '%s\n' "$PS" | awk '/MTLCompilerService/ && !/awk/ { n++; c+=$3 } END { printf "   %d process(es), total CPU %.1f%%\n", n, c }'

# --- big cache dirs ---
echo "== cache dirs"
cache_line() { # path label
  [ -d "$1" ] && [ ! -L "$1" ] || { echo "   $2: absent or a symlink (skipped)"; return; }
  echo "   $(human_size "$1")  $2"
}
cache_line "$MAIN/build/perf-bench-warm" "main build/perf-bench-warm"
for wt in "$WTROOT"/*/; do
  wt=${wt%/}; b=$wt/build
  [ -d "$b" ] && [ ! -L "$b" ] || continue
  find -P "$b" -maxdepth 2 -type d -name cache 2>/dev/null | while IFS= read -r c; do
    cache_line "$c" "$(basename "$wt")/${c#$wt/}"
  done
done
echo "(snapshot took $(( $(date +%s) - T0 )) s)"
