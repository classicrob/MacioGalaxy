#!/bin/bash
# Frees disk used by old test-run output under build/: per-run shader/pipeline cache copies and
# frozen app copies. Logs, results, CSVs, images and the small pipeline_cache.db (seed-closure evidence) are kept. Anything a running process (including
# queued lock waiters) references or has open is skipped.
# usage: build/prune-run-artifacts.sh [--dry-run] [cache-age-minutes=120] [app-age-minutes=720]  (ages use ctime: copies made with cp -p/ditto keep an old mtime)
cd "$(dirname "$0")" || exit 1
DRY=0; [ "$1" = --dry-run ] && { DRY=1; shift; }
CACHE_AGE=${1:-120}; APP_AGE=${2:-720}
ROOT=$PWD
set -f  # the exclusion patterns below must reach find unexpanded
lsof -Fn 2>/dev/null | sed -n 's/^n//p' | grep "^$ROOT" | sort -u > /tmp/prune-open.$$
ps -eo command | grep -oE "/[^ ]*\.app" | sort -u > /tmp/prune-apps.$$
keep='-not -path ./macos-gx/* -not -path ./game-data/* -not -path ./pipeline-seeds/* -not -path ./shader-seed/* -not -path ./observatory-user-2/* -not -path ./saves/*'
find . -cmin +"$CACHE_AGE" -type f \( -name 'functions*.data' -o -name 'libraries*.data' -o -name '__global__.db*' \
  -o -name 'dawn_cache.db*' \) $keep 2>/dev/null | sed "s#^\.#$ROOT#" \
  | grep -vxFf /tmp/prune-open.$$ > /tmp/prune-files.$$
find . -type d -name '*.app' -prune -cmin +"$APP_AGE" $keep 2>/dev/null | sed "s#^\.#$ROOT#" \
  | grep -vxFf /tmp/prune-apps.$$ > /tmp/prune-dirs.$$
echo "cache files: $(wc -l < /tmp/prune-files.$$), app copies: $(wc -l < /tmp/prune-dirs.$$)"
if [ $DRY = 0 ]; then
  tr '\n' '\0' < /tmp/prune-files.$$ | xargs -0 rm -f
  tr '\n' '\0' < /tmp/prune-dirs.$$ | xargs -0 rm -rf
fi
rm -f /tmp/prune-*.$$
df -h . | tail -1 | awk '{print "free: " $4}'
