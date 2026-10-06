#!/bin/zsh
# The game is now called MacioGalaxy: this only forwards to "Play MacioGalaxy.command" so old habits keep working.
exec "$(cd -- "$(dirname -- "$0")" && pwd -P)/Play MacioGalaxy.command" "$@"
