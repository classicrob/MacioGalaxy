#!/bin/bash
# Live Petari app launches, with up to N concurrent functional slots and exclusive quiet runs.
# usage: [PETARI_LOCK_CLASS=functional|quiet] build/locked-app.sh <owner-name> <command...>
#
# Slots: build/.petari-app.lock (slot 1), build/.petari-app.lock.s2, .s3 ... up to the count in
# build/.app-slots (default 1). Functional runs (default class: crash/hang/route/unlock checks) take
# any free slot. Quiet runs (timing, audio or performance evidence) need every slot free and hold
# them all, so nothing else runs beside them; while a quiet ticket is first in line, functional
# tickets behind it wait, so it cannot starve.
# Order: the ticket named in $LOCK.grant (owner name or ticket id, via build/lock-grant.sh) goes
# first; then priority from build/lock-priorities.txt (unknown owners 50) plus aging (+1 per minute
# waited, as in locked-build.sh, so no ticket starves behind a stream of higher-priority ones), ties by arrival. With a
# single slot and several waiters, the lead gets 90 s after a release to choose.
# Lock state lives in the main checkout build/ (this script's directory), so worktrees share the machine-wide locks.
LOCKROOT=$(cd "$(dirname "$0")" && pwd)
OWNER=$1; shift
CLASS=${PETARI_LOCK_CLASS:-functional}
case $CLASS in functional|quiet) ;; *) echo "PETARI_LOCK_CLASS must be functional or quiet"; exit 2;; esac
LOCK=$LOCKROOT/.petari-app.lock
QUEUE=$LOCK.queue
GRANT=$LOCK.grant
FREED=$LOCK.freed
PRIOS=$LOCKROOT/lock-priorities.txt
mkdir -p "$QUEUE"
ID="$(date +%s%N 2>/dev/null || date +%s)-$$"
TICKET="$QUEUE/$ID"
echo "$OWNER $CLASS" > "$TICKET"
HELD=()
# Test hook: print the dispatch order ("<ticket> <owner> <class>" per line) and exit; takes no slot.
PRINT_ORDER_HOOK=${LOCKED_APP_PRINT_ORDER:-}
# A person playing Petari (an app without PETARI_SMOKE or --test-fixture) limits tests to one slot,
# so test runs don't eat the player's frame rate or shader compiler.
human_playing() {
  local p
  for p in $(pgrep -f 'Contents/MacOS/Petari( |$)'); do
    ps -o comm= -p "$p" 2>/dev/null | grep -q '/Contents/MacOS/Petari$' || continue
    # The lead can list PIDs of a player's session that was left open idle (one per line).
    grep -qx "$p" "$LOCKROOT/.ignore-human-pids" 2>/dev/null && continue
    # The agent playtester's visible sessions (build/editor-playtest/...) are not a person.
    ps -ww -o command= -p "$p" 2>/dev/null | grep -q 'build/editor-playtest/' && continue
    ps -E -ww -o command= -p "$p" 2>/dev/null | grep -qE 'PETARI_SMOKE=|--test-fixture|PETARI_AGENT_RUN=1' || return 0
  done
  return 1
}
slot_count() {
  n=$(cat "$LOCKROOT/.app-slots" 2>/dev/null); [[ "$n" =~ ^[1-9]$ ]] || n=1
  [ "$n" -gt 1 ] && human_playing && n=1
  echo "$n"
}
slot_dir() { [ "$1" = 1 ] && echo "$LOCK" || echo "$LOCK.s$1"; }
cleanup() {
  rm -f "$TICKET"
  for d in "${HELD[@]}"; do
    [ "$(cat "$d/pid" 2>/dev/null)" = "$$" ] && rm -rf "$d"
  done
  [ ${#HELD[@]} -gt 0 ] && date +%s > "$FREED"
}
trap cleanup EXIT
prio() { awk -v o="$1" '!/^#/ && $1==o {print $2; f=1; exit} END {if (!f) print 50}' "$PRIOS" 2>/dev/null || echo 50; }
# Remove slots whose holder died; print the number of free slots.
free_slots() {
  local n=$(slot_count) free=0 i d
  for ((i = 1; i <= n; i++)); do
    d=$(slot_dir $i)
    if [ -f "$d/pid" ] && ! kill -0 "$(cat "$d/pid")" 2>/dev/null; then
      echo "removing stale slot $i held by $(cat "$d/owner" 2>/dev/null)" >&2; rm -rf "$d"; date +%s > "$FREED"
    fi
    [ -d "$d" ] || free=$((free + 1))
  done
  echo $free
}
# Priority plus aging: +1 per full minute waited. The ticket name starts with the arrival time in ns
# (seconds if the platform date has no %N).
eff_prio() {
  local a=${2%%-*}
  [ "${#a}" -gt 11 ] && a=$((a / 1000000000))
  echo $(( $(prio "$1") + ( $(date +%s) - a ) / 60 ))
}
# Live tickets in dispatch order: "<id> <owner> <class>".
ordered_tickets() {
  local g t n
  g=$(cat "$GRANT" 2>/dev/null)
  for t in "$QUEUE"/*; do
    [ -e "$t" ] || continue
    n=${t##*/}
    kill -0 "${n##*-}" 2>/dev/null || { rm -f "$t"; continue; }
    read -r o c < "$t"; c=${c:-functional}
    k=0; { [ -n "$g" ] && { [ "$n" = "$g" ] || [ "$o" = "$g" ]; }; } && k=1
    echo "$k $(eff_prio "$o" "$n") ${n%%-*} $n $o $c"
  done | sort -k1,1nr -k2,2nr -k3,3n | awk '{print $4, $5, $6}'
}
# Succeeds when this ticket may take slots now.
my_turn() {
  local free=$1 pos=0 id o c
  [ "$free" -gt 0 ] || return 1
  local tickets; tickets=$(ordered_tickets)
  # Single slot, several waiters, nothing granted: give the lead time to choose.
  if [ "$(slot_count)" = 1 ] && [ "$(echo "$tickets" | grep -c .)" -gt 1 ] && [ -z "$(cat "$GRANT" 2>/dev/null)" ]; then
    freed=$(cat "$FREED" 2>/dev/null || echo 0)
    [ $(( $(date +%s) - freed )) -lt 90 ] && return 1
  fi
  while read -r id o c; do
    [ -n "$id" ] || continue
    if [ "$c" = quiet ]; then
      # A quiet ticket blocks everyone behind it and runs only with every slot free.
      [ "$id" = "$ID" ] && [ "$free" = "$(slot_count)" ] && [ $pos = 0 ] && return 0
      return 1
    fi
    [ "$id" = "$ID" ] && [ $pos -lt "$free" ] && return 0
    pos=$((pos + 1))
  done <<< "$tickets"
  return 1
}
take_slots() {
  local n=$(slot_count) i d
  for ((i = 1; i <= n; i++)); do
    d=$(slot_dir $i)
    if mkdir "$d" 2>/dev/null; then
      echo $$ > "$d/pid"; echo "$OWNER" > "$d/owner"; echo "$CLASS" > "$d/class"; HELD+=("$d")
      [ "$CLASS" = functional ] && return 0
    elif [ "$CLASS" = quiet ]; then
      for d in "${HELD[@]}"; do rm -rf "$d"; done; HELD=(); return 1
    fi
  done
  [ ${#HELD[@]} -gt 0 ]
}
if [ -n "$PRINT_ORDER_HOOK" ]; then rm -f "$TICKET"; ordered_tickets; exit 0; fi
announced=""
while true; do
  free=$(free_slots)
  if my_turn "$free" && take_slots; then break; fi
  holders=$(for ((i = 1; i <= $(slot_count); i++)); do cat "$(slot_dir $i)/owner" 2>/dev/null; done | paste -sd, -)
  [ "$announced" != "$holders" ] && echo "waiting for app slot ($CLASS; held by ${holders:-nobody}; lead dispatches; fallback priority $(prio "$OWNER"))" && announced=$holders
  sleep 2
done
rm -f "$TICKET"
g=$(cat "$GRANT" 2>/dev/null); { [ "$g" = "$ID" ] || [ "$g" = "$OWNER" ]; } && rm -f "$GRANT"
# Cap each locked command so a hung test or app cannot hold a slot indefinitely.
export PETARI_AGENT_RUN=1  # marks agent runs so human_playing() doesn't count them
# Quiet runs also hold the sweep lane and build lock, so compiles and ctest can't load the machine during them.
if [ "$CLASS" = quiet ] && [ -z "$PETARI_BUILD_LOCK_HELD" ]; then
  # Lock order app -> sweep lane -> build, as the timing tools require.
  # locked-build.sh holds every build slot only for owners named "quiet*", so pass quiet-<owner>.
  "$LOCKROOT/locked-sweep-lane.sh" "$OWNER" "$LOCKROOT/locked-build.sh" "quiet-$OWNER" /usr/bin/perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "${PETARI_LOCK_TIMEOUT:-2700}" "$@"
else
  /usr/bin/perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "${PETARI_LOCK_TIMEOUT:-2700}" "$@"
fi
