#!/bin/bash
# usage: run-with-hang-sampler.sh <log> <user-dir>
LOG=$1; USERDIR=$2
PETARI_SMOKE=galaxy PETARI_SMOKE_FRAMES=108000 PETARI_TRACE_BOOT=1 PETARI_AUDIO_DIAG=1 \
  build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari \
  --disc build/game-data/RMGE01 --user "$USERDIR" --test-fixture observatory > "$LOG" 2>&1 &
PID=$!
last=""; same=0; sampled=0
while kill -0 $PID 2>/dev/null; do
  sleep 3
  cur=$(strings "$LOG" | grep -E "^Petari trace: frame|PETARI SMOKE \[frame" | tail -1)
  if [ "$cur" = "$last" ]; then same=$((same+3)); else same=0; last="$cur"; fi
  if [ $same -ge 18 ] && [ $sampled -lt 2 ]; then
    sample $PID 4 -file "${LOG%.log}-hang-sample-$sampled.txt" >/dev/null 2>&1
    sampled=$((sampled+1)); same=0
  fi
done
wait $PID; echo "exit=$?"
