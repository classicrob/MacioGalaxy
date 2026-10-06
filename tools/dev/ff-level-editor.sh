#!/bin/bash
# Fast-forward level-editor to a tested merge commit, safely.
# usage: build/ff-level-editor.sh <tested-sha>
# Refuses unless the current level-editor tip is an ancestor of <tested-sha> (so nothing is dropped);
# the update is atomic against the tip it checked (fails if someone moved it meanwhile: re-merge, re-test, retry).
set -euo pipefail
cd "$(dirname "$0")/.."
new=$(git rev-parse --verify "$1^{commit}")
tip=$(git rev-parse --verify refs/heads/level-editor)
if ! git merge-base --is-ancestor "$tip" "$new"; then
  echo "ff-level-editor: REFUSED: level-editor ($tip) is not contained in $new; merge level-editor into your branch, re-test, retry" >&2
  exit 1
fi
git update-ref -m "ff-level-editor $new" refs/heads/level-editor "$new" "$tip"
echo "ff-level-editor: level-editor $tip -> $new"
