#!/bin/bash
# Serialize every configure/build/ctest and other heavy CPU work across workers.
# Lead-dispatched queue: waiters take a ticket. When the lock is free, the ticket named in
# $LOCK.grant (owner name or ticket id, written by the lead via build/lock-grant.sh) goes next.
# If no live ticket is granted within 0 s of the lock freeing (or only one is waiting), the highest priority in
# build/lock-priorities.txt goes (ties by arrival; unknown owners 50).
# Slots: PETARI_BUILD_SLOTS (default 2) commands may hold the build lock at once; slot 1 is .macos-gx.lock,
# slot k is .macos-gx.lock.<k>. An owner named "quiet*" waits for every slot and holds them all (timing runs).
# usage: build/locked-build.sh <owner-name> <command...>
# Lock state lives in the main checkout build/ (this script's directory), so worktrees share the machine-wide locks.
LOCKROOT=$(cd "$(dirname "$0")" && pwd)
OWNER=$1; shift
# Re-entrant: a command already running under the build lock (e.g. a quiet app run) runs directly.
if [ -n "$PETARI_BUILD_LOCK_HELD" ]; then exec "$@"; fi
# PETARI_BUILD_BACKGROUND=1: a build (not tests, not app runs) at darwin background QoS (`taskpolicy -b`: efficiency
# cores only, low I/O priority) in its own slot, .macos-gx.lock.bg, one at a time. It does not use or wait for the
# normal slots, so it also runs beside quiet holders. Measured 2026-10-17 (perf_bench ab, 3 stages x 2 repeats warm +
# EggStar cold x 2): r2r, busy and present stats within repeat noise; a -j4 compile ran ~3x slower than normal QoS.
if [ "${PETARI_BUILD_BACKGROUND:-0}" = 1 ]; then
  case "$(basename "$1")" in
    cmake) [ "$2" = --build ] || { echo "PETARI_BUILD_BACKGROUND: only 'cmake --build', make or ninja" >&2; exit 2; } ;;
    make|ninja) ;;
    *) echo "PETARI_BUILD_BACKGROUND: only 'cmake --build', make or ninja (no ctest or app runs)" >&2; exit 2 ;;
  esac
  BG=$LOCKROOT/.macos-gx.lock.bg
  announced=""
  until mkdir "$BG" 2>/dev/null; do
    if [ -f "$BG/pid" ] && ! kill -0 "$(cat "$BG/pid")" 2>/dev/null; then rm -rf "$BG"; continue; fi
    [ -z "$announced" ] && echo "waiting for the background build slot (held by $(cat "$BG/owner" 2>/dev/null))" && announced=1
    sleep 2
  done
  echo $$ > "$BG/pid"; echo "$OWNER" > "$BG/owner"; echo background > "$BG/class"
  trap 'rm -rf "$BG"' EXIT
  # Never point it at a build dir a normal-slot build may be using (each worktree has its own; main is the lead's).
  # Four efficiency cores: more jobs only add memory pressure. An explicit -j in the command still wins.
  export CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-4}
  PETARI_BUILD_LOCK_HELD=1 /usr/sbin/taskpolicy -b /usr/bin/perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "${PETARI_LOCK_TIMEOUT:-2700}" "$@"
  exit $?
fi
LOCK=$LOCKROOT/.macos-gx.lock
QUEUE=$LOCK.queue
GRANT=$LOCK.grant
FREED=$LOCK.freed
PRIOS=$LOCKROOT/lock-priorities.txt
mkdir -p "$QUEUE"
ID="$(date +%s%N 2>/dev/null || date +%s)-$$"
TICKET="$QUEUE/$ID"
echo "$OWNER" > "$TICKET"
cleanup() {
  rm -f "$TICKET"
  for d in $HELD; do
    if [ "$(cat "$d/pid" 2>/dev/null)" = "$$" ]; then rm -rf "$d"; date +%s > "$FREED"; fi
  done
}
trap cleanup EXIT
prio() { awk -v o="$1" '!/^#/ && $1==o {print $2; f=1; exit} END {if (!f) print 50}' "$PRIOS" 2>/dev/null || echo 50; }
live_tickets() {
  for t in "$QUEUE"/*; do
    [ -e "$t" ] || continue
    n=${t##*/}
    kill -0 "${n##*-}" 2>/dev/null || { rm -f "$t"; continue; }
    echo "$n $(cat "$t" 2>/dev/null)"
  done
}
next_ticket() {
  g=$(cat "$GRANT" 2>/dev/null)
  if [ -n "$g" ]; then
    m=$(live_tickets | awk -v g="$g" '$1==g || $2==g' | sort -n | head -1 | awk '{print $1}')
    [ -n "$m" ] && { echo "$m"; return; }
  fi
  # Give the lead a grace period to choose, unless there is no choice to make.
  freed=$(cat "$FREED" 2>/dev/null || echo 0)
  [ "$(live_tickets | wc -l)" -gt 1 ] && [ $(( $(date +%s) - freed )) -lt 0 ] && return
  live_tickets | while read n o; do echo "$(prio "$o") ${n%%-*} $n"; done | sort -k1,1nr -k2,2n | head -1 | awk '{print $3}'
}
SLOTS=${PETARI_BUILD_SLOTS:-2}
slot_dir() { [ "$1" = 1 ] && echo "$LOCK" || echo "$LOCK.$1"; }
HELD=""
# My turn when my rank in the queue (grant first, then priority, then arrival) is below the number of free slots.
ordered_tickets() {
  g=$(cat "$GRANT" 2>/dev/null)
  live_tickets | while read n o; do
    # Aging: +1 priority per minute waited, so nobody starves behind a stream of higher-priority builds.
    if [ -n "$g" ] && { [ "$n" = "$g" ] || [ "$o" = "$g" ]; }; then echo "999999 ${n%%-*} $n"
    else echo "$(( $(prio "$o") + ( $(date +%s) - ${n%%-*} / 1000000000 ) / 60 )) ${n%%-*} $n"; fi
  done | sort -k1,1nr -k2,2n | awk '{print $3}'
}
my_turn() {
  free=0; for k in $(seq 1 "$SLOTS"); do [ -d "$(slot_dir "$k")" ] || free=$((free+1)); done
  [ "$free" -gt 0 ] || return 1
  case $OWNER in quiet*) [ "$free" -eq "$SLOTS" ] || return 1; free=1;; esac
  pos=$(ordered_tickets | awk -v id="$ID" '$1==id {print NR-1; exit}')
  [ -n "$pos" ] && [ "$pos" -lt "$free" ]
}
announced=""
while true; do
  # Clear stale slots (holder process gone).
  for k in $(seq 1 "$SLOTS"); do
    d=$(slot_dir "$k")
    if [ -f "$d/pid" ] && ! kill -0 "$(cat "$d/pid")" 2>/dev/null; then
      echo "removing stale lock slot $k held by $(cat "$d/owner" 2>/dev/null)"; rm -rf "$d"; date +%s > "$FREED"
    fi
  done
  if my_turn; then
    case $OWNER in
      quiet*)  # exclusive: take every slot or none
        got=""
        for k in $(seq 1 "$SLOTS"); do d=$(slot_dir "$k"); mkdir "$d" 2>/dev/null && { echo $$ > "$d/pid"; echo "$OWNER" > "$d/owner"; got="$got $d"; } || break; done
        if [ "$(echo $got | wc -w)" -eq "$SLOTS" ]; then HELD=$got; break; fi
        for d in $got; do rm -rf "$d"; done ;;
      *)
        for k in $(seq 1 "$SLOTS"); do d=$(slot_dir "$k"); if mkdir "$d" 2>/dev/null; then HELD=$d; break 2; fi; done ;;
    esac
  fi
  holder=$(for k in $(seq 1 "$SLOTS"); do cat "$(slot_dir "$k")/owner" 2>/dev/null; done | paste -sd, -)
  [ "$announced" != "$holder" ] && echo "waiting for lock (held by ${holder:-nobody}; lead dispatches; fallback priority $(prio "$OWNER"))" && announced=$holder
  sleep 2
done
for d in $HELD; do echo $$ > "$d/pid"; echo "$OWNER" > "$d/owner"; done; rm -f "$TICKET"
g=$(cat "$GRANT" 2>/dev/null); { [ "$g" = "$ID" ] || [ "$g" = "$OWNER" ]; } && rm -f "$GRANT"
# Cap each locked command so a hung test or app cannot hold the lock indefinitely.
PETARI_BUILD_LOCK_HELD=1 /usr/bin/perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "${PETARI_LOCK_TIMEOUT:-2700}" "$@"
