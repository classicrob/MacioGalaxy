# Dev scripts of the agent team

These scripts keep several agents from running heavy work at once and manage the git
worktrees they use. They are used by the agent team, **not needed to play**.

## Run them through `build/`, never from here

The lock scripts keep their state (`.app-slots`, lock directories, tickets, priorities,
`lock-priorities.txt`) next to themselves, computed from `dirname "$0"`. In the main checkout
each file here is reached through a symlink `build/<name>.sh -> ../tools/dev/<name>.sh`, so
`dirname "$0"` stays `build/` and the state stays in `build/`. Run `build/<name>.sh`.
Running `tools/dev/<name>.sh` directly would start a second, separate lock state in
`tools/dev/` and defeat the lock. Some scripts also find the checkout as `dirname "$0"/..`.

(The header comments inside some scripts still say "untracked dev script in build/"; that was
true before they moved here.)

## Locks and queues

| Script | What it does |
| --- | --- |
| `locked-build.sh` | Serializes every configure, build, ctest and other heavy CPU job; waiters take a ticket. |
| `locked-app.sh` | Live app launches: up to N concurrent functional slots (`build/.app-slots`) or one exclusive quiet run (`PETARI_LOCK_CLASS`). |
| `locked-sweep-lane.sh` | Second app lane for functional stage-sweep runs. |
| `locked-soak-lane.sh` | Third app lane for long functional soak runs. |
| `lock-status.sh` | Shows lock holders and waiting tickets with their priorities. |
| `lock-grant.sh` | Lead only: chooses which owner or ticket takes a lock next. |

## Worktrees and branches

| Script | What it does |
| --- | --- |
| `new-worktree.sh` | Creates a configured worktree `../macio-worktrees/<name>` on `wt/<name>` (see `docs/dev/WORKTREES.md`). |
| `remove-worktree.sh` | Removes a merged worktree and deletes its branch; never forces. |
| `head-check.sh` | Fast-forwards the `head-check` worktree to main, builds everything there and runs ctest. |
| `ff-level-editor.sh` | Fast-forwards `level-editor` to a tested merge commit; refuses if it would drop commits. |

## Housekeeping

| Script | What it does |
| --- | --- |
| `prune-run-artifacts.sh` | Frees disk used by old per-run caches and frozen app copies under `build/`. |
| `prune-run-artifacts-caches.sh` | Removes finished runs' private `cache` dirs in `build/` and every worktree's `build/`. |
| `run-with-hang-sampler.sh` | `<log> <user-dir>`: runs a long galaxy smoke on the observatory fixture and, when its log stops advancing, takes macOS `sample` stacks of the app. Run it from the checkout root; it launches the app itself, not through `locked-app.sh`. |

## Checking and watching

| Script | What it does |
| --- | --- |
| `verify-main.sh` | `[--dry-run] [BUILD_DIR] [OWNER]` (defaults: main's `build/macos-gx`, owner `lead`). The integrator's one-command check: finds every ctest executable (`ctest --show-only=json-v1`, plus `CTestTestfile.cmake` for binaries not built yet), builds `petari` and each test target, deletes and relinks binaries whose dylibs vanished (`otool -L`, e.g. after a Homebrew upgrade), runs `ctest -j4 --timeout 600 -LE timing`, and prints pass/fail counts with failures split into dyld/stale and real. Builds and ctest run through the **main** checkout's `build/locked-build.sh <owner>`. Logs: `<BUILD_DIR>/verify-main-{build,ctest}.log`. Exit 0 pass, 1 test failures, 2 setup/build error. `--dry-run` only prints the plan; `--selftest` runs its built-in tests. |
| `team-status.sh` | Read-only snapshot in about a second, for the lead: free disk against the 40 GB floor (warns below 45), each worktree's size, commits ahead of main and dirty tracked files, `lock-status.sh`, running Petari apps with owner and elapsed time, MTLCompilerService CPU, and big cache dirs. It never walks into `~/Space` (symlinked dirs are skipped, `find -P` with depth limits). |

Both are reached as `build/verify-main.sh` and `build/team-status.sh` (create the symlinks in the main
checkout: `cd build && ln -s ../tools/dev/verify-main.sh . && ln -s ../tools/dev/team-status.sh .`).
From a worktree they find the main checkout through git, so running `tools/dev/<name>.sh` there is also fine
for these two (they keep no state). Tests: `tools/dev/tests/test-verify-and-status.sh [BUILD_DIR]`.
