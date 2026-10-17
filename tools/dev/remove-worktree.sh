#!/bin/bash
# Remove a worktree after its branch is merged (or to discard it). Never forces.
# usage: build/remove-worktree.sh <name>
#   git worktree remove (refuses with uncommitted/untracked work) + git branch -d (refuses if unmerged).
set -euo pipefail
NAME=${1:-}
MAIN=$(cd "$(dirname "$0")/.." && pwd -P)
WT=${PETARI_WORKTREE_ROOT:-$(dirname "$MAIN")/macio-worktrees}/$NAME
[[ "$NAME" =~ ^[a-z0-9][a-z0-9-]{0,30}$ ]] || { echo "usage: build/remove-worktree.sh <name>" >&2; exit 1; }
[ -d "$WT" ] || { echo "remove-worktree: $WT does not exist" >&2; exit 1; }
git -C "$MAIN" worktree remove "$WT"
git -C "$MAIN" branch -d "wt/$NAME"
git -C "$MAIN" worktree prune
echo "removed $WT and branch wt/$NAME"
