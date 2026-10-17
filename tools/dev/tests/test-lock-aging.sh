#!/bin/bash
# Tests for ticket aging in locked-app.sh and the sweep/soak lanes. Uses a scratch LOCKROOT (a copy of
# the scripts in a temp dir) with fake tickets whose arrival time is backdated; never touches main's build/.
set -u
DEV=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d "${TMPDIR:-/tmp}/lock-aging.XXXXXX")
PIDS=()
trap 'kill "${PIDS[@]}" 2>/dev/null; rm -rf "$S"' EXIT
cp "$DEV"/locked-app.sh "$DEV"/locked-sweep-lane.sh "$DEV"/locked-soak-lane.sh "$S"/
cat > "$S/lock-priorities.txt" <<'P'
frame-cost 85
perf-bench 80
cold-hitch 70
P
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
now=$(date +%s)
# ticket <queue-dir> <owner> <class|""> <minutes-ago>  -> prints the ticket name (arrival ns - live pid)
ticket() {
  sleep 600 >/dev/null 2>&1 & local pid=$!; disown "$pid"; PIDS+=("$pid")
  local name="$(( (now - $4 * 60) ))000000000-$pid"
  mkdir -p "$1"; echo "$2${3:+ $3}" > "$1/$name"; echo "$name"
}
reset() { rm -rf "$S"/.petari-*; }
order() { LOCKED_APP_PRINT_ORDER=1 "$S/locked-app.sh" probe 2>&1 | awk '{print $2}' | paste -sd' ' -; }

Q=$S/.petari-app.lock.queue
# 1. no waiting: higher priority first
reset; ticket $Q cold-hitch quiet 0 >/dev/null; ticket $Q frame-cost functional 0 >/dev/null; ticket $Q perf-bench functional 0 >/dev/null
[ "$(order)" = "frame-cost perf-bench cold-hitch" ] && ok "fresh tickets: plain priority order" || { bad "fresh order: $(order)"; }
# 2. cold-hitch (70) waited 20 min -> 90 beats frame-cost 85 fresh
reset; ticket $Q cold-hitch quiet 20 >/dev/null; ticket $Q frame-cost functional 0 >/dev/null; ticket $Q perf-bench functional 0 >/dev/null
[ "$(order)" = "cold-hitch frame-cost perf-bench" ] && ok "aged quiet ticket rises to the head" || bad "aged order: $(order)"
# 3. waited only 10 min -> 80 < 85, still behind frame-cost but ahead of nothing it can't beat; perf-bench 80 vs 80 tie -> earlier arrival wins
reset; ticket $Q cold-hitch quiet 10 >/dev/null; ticket $Q frame-cost functional 0 >/dev/null; ticket $Q perf-bench functional 0 >/dev/null
[ "$(order)" = "frame-cost cold-hitch perf-bench" ] && ok "10 min of aging: tie broken by arrival" || bad "tie order: $(order)"
# 4. a stream of newer high-priority tickets cannot starve it forever (1 hour)
reset; ticket $Q cold-hitch quiet 60 >/dev/null; for i in 1 2 3; do ticket $Q frame-cost functional $i >/dev/null; done
[ "$(order | awk '{print $1}')" = "cold-hitch" ] && ok "one hour wait beats a stream of 85s" || bad "starved: $(order)"
# 5. an explicit grant still goes first
reset; ticket $Q cold-hitch quiet 60 >/dev/null; ticket $Q perf-bench functional 0 >/dev/null; echo perf-bench > "$S/.petari-app.lock.grant"
[ "$(order | awk '{print $1}')" = "perf-bench" ] && ok "grant overrides aging" || bad "grant: $(order)"
# 6. a quiet ticket at the head blocks functional tickets behind it (unchanged my_turn rule): head is quiet
reset; ticket $Q cold-hitch quiet 30 >/dev/null; ticket $Q frame-cost functional 0 >/dev/null
first=$(LOCKED_APP_PRINT_ORDER=1 "$S/locked-app.sh" probe | head -1)
case $first in *" cold-hitch quiet") ok "quiet head ordered first (functional tickets wait behind it)";; *) bad "quiet head: $first";; esac

# 7. sweep and soak lanes
for lane in sweep soak; do
  reset; Q2=$S/.petari-$lane-lane.lock.queue
  a=$(ticket $Q2 cold-hitch "" 30); b=$(ticket $Q2 frame-cost "" 0)
  got=$(LOCKED_LANE_PRINT_NEXT=1 "$S/locked-$lane-lane.sh" probe)
  [ "$got" = "$a" ] && ok "$lane lane: aged ticket next" || bad "$lane lane: got '$got' want '$a'"
  reset; a=$(ticket $Q2 cold-hitch "" 5); b=$(ticket $Q2 frame-cost "" 0)
  got=$(LOCKED_LANE_PRINT_NEXT=1 "$S/locked-$lane-lane.sh" probe)
  [ "$got" = "$b" ] && ok "$lane lane: fresh high priority still first" || bad "$lane lane: got '$got' want '$b'"
done
exit $fail
