#!/bin/bash
# Third app lane for long functional soak runs (in-process loop, no relaunches), started by the lead only when no quiet measurement is pending.
# Lead-dispatched queue: waiters take a ticket. When the lock is free, the ticket named in
# $LOCK.grant (owner name or ticket id, written by the lead via build/lock-grant.sh) goes next.
# If no live ticket is granted within 90 s of the lock freeing (or only one is waiting), the highest priority in
# build/lock-priorities.txt plus 1 per minute waited goes (ties by arrival; unknown owners 50).
# usage: build/locked-sweep-lane.sh <owner-name> <command...>
# Lock state lives in the main checkout build/ (this script's directory), so worktrees share the machine-wide locks.
LOCKROOT=$(cd "$(dirname "$0")" && pwd)
OWNER=$1; shift
LOCK=$LOCKROOT/.petari-soak-lane.lock
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
  if [ "$(cat "$LOCK/pid" 2>/dev/null)" = "$$" ]; then rm -rf "$LOCK"; date +%s > "$FREED"; fi
}
trap cleanup EXIT
prio() { awk -v o="$1" '!/^#/ && $1==o {print $2; f=1; exit} END {if (!f) print 50}' "$PRIOS" 2>/dev/null || echo 50; }
# Priority plus aging (+1 per full minute waited, as in locked-build.sh) so no ticket starves.
eff_prio() {
  local a=${2%%-*}
  [ "${#a}" -gt 11 ] && a=$((a / 1000000000))
  echo $(( $(prio "$1") + ( $(date +%s) - a ) / 60 ))
}
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
  [ "$(live_tickets | wc -l)" -gt 1 ] && [ $(( $(date +%s) - freed )) -lt 90 ] && return
  live_tickets | while read n o; do echo "$(eff_prio "$o" "$n") ${n%%-*} $n"; done | sort -k1,1nr -k2,2n | head -1 | awk '{print $3}'
}
# Test hook: print the ticket that would run next and exit; takes no lock.
if [ -n "${LOCKED_LANE_PRINT_NEXT:-}" ]; then rm -f "$TICKET"; next_ticket; exit 0; fi
announced=""
while true; do
  if [ ! -d "$LOCK" ] && [ "$(next_ticket)" = "$ID" ] && mkdir "$LOCK" 2>/dev/null; then break; fi
  if [ -f "$LOCK/pid" ] && ! kill -0 "$(cat "$LOCK/pid")" 2>/dev/null; then
    echo "removing stale lock held by $(cat "$LOCK/owner" 2>/dev/null)"; rm -rf "$LOCK"; date +%s > "$FREED"; continue
  fi
  holder=$(cat "$LOCK/owner" 2>/dev/null)
  [ "$announced" != "$holder" ] && echo "waiting for lock (held by ${holder:-nobody}; lead dispatches; fallback priority $(prio "$OWNER"))" && announced=$holder
  sleep 2
done
echo $$ > "$LOCK/pid"; echo "$OWNER" > "$LOCK/owner"; rm -f "$TICKET"
g=$(cat "$GRANT" 2>/dev/null); { [ "$g" = "$ID" ] || [ "$g" = "$OWNER" ]; } && rm -f "$GRANT"
# Cap each locked command so a hung test or app cannot hold the lock indefinitely.
/usr/bin/perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "${PETARI_LOCK_TIMEOUT:-2700}" "$@"
