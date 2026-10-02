#!/bin/bash
# Viewport backgrounds for the level editor mockup (index.html), captured from your own game.
# Game imagery: written to build/level-editor-ux/ (git-ignored), never committed.
#
# usage: docs/dev/level-editor/mockups/capture-viewports.sh   (from anywhere; needs a built app and build/game-data)
#
# For each galaxy: the stage smoke loads mission 1, then the photo-mode test driver enters photo mode.
# The XFB is dumped every 3 frames for 36 frames after entry; frame 9 is HUD-less and still at the
# game's camera, and becomes build/level-editor-ux/viewport-<name>.jpg (1920x1080).
set -eu
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
cd "$ROOT"
R=build/level-editor-ux
APP=${PETARI_APP:-build/macos-gx/native/app/Petari.app}
mkdir -p $R
# A private copy of the app, so a concurrent rebuild can't change the binary mid-run.
rm -rf $R/Petari.app && cp -R "$APP" $R/Petari.app
run() { name=$1 stage=$2; U=$R/user-$name; D=$R/xfb-$name; S=$R/shots-$name
  rm -rf $U $D $S; mkdir -p $D $S
  python3 native/tools/create_observatory_fixture.py --source build/observatory-user-2 --output $U --kind stage > /dev/null
  env PETARI_PHOTO_MODE=1 PETARI_PHOTO_TEST=1700 PETARI_PHOTO_DIR=$PWD/$S \
    PETARI_XFB_DUMP=$PWD/$D PETARI_XFB_DUMP_LABELS=photo PETARI_XFB_DUMP_EVERY=3 PETARI_XFB_DUMP_SPAN=36 \
    PETARI_SMOKE=stage PETARI_STAGE=$stage PETARI_SCENARIO=1 PETARI_STAGE_IDLE_FRAMES=2000 PETARI_SMOKE_FRAMES=12000 \
    PETARI_LOCK_CLASS=functional build/locked-app.sh leveleditor-ux /usr/bin/perl -e 'alarm shift; exec @ARGV or die' 900 \
    $R/Petari.app/Contents/MacOS/Petari --disc build/game-data/RMGE01 --user $PWD/$U --test-fixture stage > $R/$name.log 2>&1 || true
  if [ -f $D/xfb-photo-9.png ]; then
    sips -s format jpeg -s formatOptions 88 --resampleWidth 1920 $D/xfb-photo-9.png --out $R/viewport-$name.jpg > /dev/null
    echo "$name: $R/viewport-$name.jpg"
  else
    echo "$name: no frame (see $R/$name.log)"
  fi
}
run gusty CosmosGardenGalaxy
run spacejunk StarDustGalaxy
run goodegg EggStarGalaxy
