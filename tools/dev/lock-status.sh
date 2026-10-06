#!/bin/bash
# Show lock holders and waiting tickets with their priorities (app slots: build/.app-slots).
PRIOS=build/lock-priorities.txt
slots=$(cat build/.app-slots 2>/dev/null); [[ "$slots" =~ ^[1-9]$ ]] || slots=1
for L in build/.macos-gx.lock build/.macos-gx.lock.2 build/.petari-app.lock build/.petari-sweep-lane.lock build/.petari-soak-lane.lock; do
  if [ "$L" = build/.petari-app.lock ]; then
    held=$(for ((i = 1; i <= slots; i++)); do d=$L; [ $i = 1 ] || d=$L.s$i; [ -d "$d" ] && echo "$(cat $d/owner 2>/dev/null)($(cat $d/class 2>/dev/null || echo functional))"; done | paste -sd' ' -)
    echo "== $L [$slots slot(s)]: held by ${held:-nobody}"
  else
    echo "== $L: held by $(cat $L/owner 2>/dev/null || echo nobody)"
  fi
  for t in $L.queue/*; do [ -e "$t" ] || continue; read -r o c < "$t"; p=$(awk -v o="$o" '!/^#/ && $1==o {print $2; f=1; exit} END {if (!f) print 50}' $PRIOS); echo "   $p $o${c:+ [$c]} (${t##*-})"; done | sort -k1,1nr
done
