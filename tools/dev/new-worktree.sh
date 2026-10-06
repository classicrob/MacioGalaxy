#!/bin/bash
# Create an isolated git worktree for a worker (convention: docs/dev/WORKTREES.md).
# usage: build/new-worktree.sh <name> [base-ref=main] [branch=wt/<name>]
#   -> <repo-parent>/petari-worktrees/<name> on the branch (default wt/<name>), configured in build/macos-gx.
# Untracked dev script: lives in the main checkout's build/ next to the lock scripts.
set -euo pipefail
trap '' PIPE   # a reader that closes early (| head) must not kill the setup half way

NAME=${1:-}
BASE=${2:-main}
MAIN=$(cd "$(dirname "$0")/.." && pwd -P)
PARENT=$(dirname "$MAIN")
ROOT=$PARENT/petari-worktrees
MAX=${NEW_WORKTREE_MAX:-12}  # worktrees under petari-worktrees/ (including head-check); lead-approved while free disk > 30 GiB
MIN_FREE_GIB=10

die() { echo "new-worktree: $*" >&2; exit 1; }
say() { { echo "$@" >&3; } 2>/dev/null || true; }   # to the caller's stdout; a closed pipe is ignored

[[ "$NAME" =~ ^[a-z0-9][a-z0-9-]{0,30}$ ]] || die "usage: build/new-worktree.sh <name> [base-ref]  (name: lowercase letters, digits, dashes)"
[ -d "$MAIN/.git" ] || die "$MAIN is not the main checkout"
case "$ROOT" in "$HOME/Space"*) die "worktrees must not live under ~/Space" ;; esac

WT=$ROOT/$NAME
BRANCH=${3:-wt/$NAME}
[ ! -e "$WT" ] || die "$WT already exists"
git -C "$MAIN" show-ref --verify --quiet "refs/heads/$BRANCH" && die "branch $BRANCH already exists"
git -C "$MAIN" rev-parse --verify --quiet "$BASE^{commit}" >/dev/null || die "base ref $BASE does not exist"

mkdir -p "$ROOT"
# All verbose output (git, cmake, lock waits) goes to a log file, not to a pipe that may close.
LOG="$ROOT/.setup-$NAME.log"
exec 3>&1
exec >"$LOG" 2>&1
say "new-worktree: log: $LOG"
COUNT=$(git -C "$MAIN" worktree list --porcelain | grep -c "^worktree $ROOT/" || true)
[ "$COUNT" -lt "$MAX" ] || die "$COUNT worktrees already exist under $ROOT (max $MAX); merge or remove one first"
FREE_GIB=$(df -k "$ROOT" | awk 'NR==2 {print int($4/1048576)}')
[ "$FREE_GIB" -ge "$MIN_FREE_GIB" ] || die "only $FREE_GIB GiB free (need $MIN_FREE_GIB; a build dir is about 3.5 GiB)"

git -C "$MAIN" worktree add -b "$BRANCH" "$WT" "$BASE"
BASE_HASH=$(git -C "$WT" rev-parse --short HEAD)

# Game data is shared (about 9 GiB): the worktree's build/game-data points at the main checkout's.
mkdir -p "$WT/build"
[ -e "$MAIN/build/game-data" ] && ln -s "$MAIN/build/game-data" "$WT/build/game-data"

# Reuse the main checkout's already-fetched sources (Aurora, SDL, Dawn prebuilt, ImGui) instead of
# downloading them again. Only the immutable *-src trees are shared: the *-build trees next to them
# are per-build outputs, so each worktree keeps its own.
DEPS=$MAIN/build/aurora/_deps
SRC_ARGS=()
if [ -d "$MAIN/../aurora-reference" ]; then
    SRC_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_AURORA=$(cd "$MAIN/../aurora-reference" && pwd -P)")
else
    echo "new-worktree: warning: $MAIN/../aurora-reference not found; Aurora will be downloaded" >&2
fi
for src in "$DEPS"/*-src; do
    [ -d "$src" ] || continue
    pkg=$(basename "$src" -src | tr 'a-z-' 'A-Z_')
    SRC_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_$pkg=$src")
done
[ -d "$DEPS/imgui-src" ] && SRC_ARGS+=("-DPETARI_IMGUI_SOURCE_DIR=$DEPS/imgui-src")
[ -d "$MAIN/build/pipeline-seeds" ] && SRC_ARGS+=("-DPETARI_PIPELINE_STAGE_SEEDS=$MAIN/build/pipeline-seeds")
SEED=$(sed -n 's/^PETARI_PIPELINE_SEED:FILEPATH=//p' "$MAIN/build/macos-gx/CMakeCache.txt" 2>/dev/null || true)
[ -n "$SEED" ] && [ -f "$SEED" ] && SRC_ARGS+=("-DPETARI_PIPELINE_SEED=$SEED")

say "new-worktree: configuring $WT/build/macos-gx (through the main build lock; this can wait for the lock)"
# Shared compiler cache: worktrees share most sources, so one compile serves all of them
# (ccache base_dir is the workspace root, so paths differing per worktree still hit).
CCACHE_ARGS=()
command -v ccache >/dev/null && CCACHE_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_OBJC_COMPILER_LAUNCHER=ccache -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache)
set +e
(cd "$WT" && "$MAIN/build/locked-build.sh" "wt-$NAME" cmake -S . -B build/macos-gx \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DPETARI_BUILD_GX_PROBE=ON \
    -DCMAKE_PREFIX_PATH="$(brew --prefix sqlite)" \
    -DFETCHCONTENT_UPDATES_DISCONNECTED=ON "${CCACHE_ARGS[@]}" "${SRC_ARGS[@]}")
CONFIGURE_STATUS=$?
set -e
# Fail loudly: a configure that did not finish leaves a worktree that cannot build.
if [ "$CONFIGURE_STATUS" -ne 0 ] || [ ! -f "$WT/build/macos-gx/CMakeCache.txt" ] || [ ! -f "$WT/build/macos-gx/Makefile" ]; then
    say "new-worktree: CONFIGURE FAILED (exit $CONFIGURE_STATUS); $WT is NOT usable. Last lines of $LOG:"
    { tail -15 "$LOG" >&3; } 2>/dev/null || true
    say "new-worktree: fix the cause, then re-run: cd $WT && $MAIN/build/locked-build.sh wt-$NAME cmake -S . -B build/macos-gx (same flags), or build/remove-worktree.sh $NAME"
    exit 1
fi

{ cat <<EOF >&3; } 2>/dev/null || true

Worktree ready: $WT   (branch $BRANCH, from $BASE @ $BASE_HASH)
Only committed work on $BASE is in it; uncommitted changes in the main checkout are not.

Work and commit inside the worktree (path-limited commits are fine there). Heavy CPU still goes
through the MAIN checkout's lock scripts, called by absolute path from the worktree directory:

  cd $WT
  $MAIN/build/locked-build.sh $NAME cmake --build build/macos-gx -j6 --target petari
  $MAIN/build/locked-build.sh $NAME ctest --test-dir build/macos-gx -j4 --timeout 600 -LE timing
  PETARI_LOCK_CLASS=functional $MAIN/build/locked-app.sh $NAME <command that runs the app>

(The default build does not relink the app: build with --target petari.) The game disc is at
$WT/build/game-data (a symlink to the main checkout's). When done, tell the integrator
"MERGE-READY: $BRANCH; tests: ..." with the list of tests you ran.
EOF
