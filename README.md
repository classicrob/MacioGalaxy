# MacioGalaxy: Super Mario Galaxy for Apple Silicon (unofficial native port)

**MacioGalaxy** runs **Super Mario Galaxy** natively on Apple Silicon Macs. The
original game code is compiled for arm64 and runs directly on your Mac,
rendering with Metal through [Aurora](https://github.com/encounter/aurora), with
native input, audio and menus around it. There is no emulator. It needs your own
copy of the game's data (see [Your game data](#your-game-data)).

> [!NOTE]
> **Built on the SMGCommunity [Petari](https://github.com/SMGCommunity/Petari)
> decompilation, with thanks.** MacioGalaxy exists because of the Petari
> contributors' years of work reconstructing the game's source (see
> [Built on Petari](#built-on-petari)). The decompiled game code in this
> repository is theirs (CC0 1.0); the native macOS port around it is this
> project.

## What it adds

- **Native Apple Silicon and Metal:** arm64 game code, GX rendered through
  Aurora/Dawn on Metal, native audio, no emulator.
- **Keyboard, mouse and game controller**, with in-game remapping:
  [native/CONTROLS.md](native/CONTROLS.md).
- **A native Home menu** (**F1** or **`**): Resume, Controls (the remapper),
  Mods (which also holds Level Select, to jump to any galaxy, and the camera
  options), My Progress (a personal record of cleared missions), Restart and Quit.
- **Optional mods**, all off by default ([native/MODS.md](native/MODS.md)):
  - hold **Space** for 1.5 s to **skip cutscenes and dialogue**;
  - **point to move**: hold the left mouse button and Mario runs toward the cursor;
  - **Odyssey movement** (Super Mario Odyssey's physics and moveset), **Cappy**
    (cap throw and bounce) and an **Odyssey-style orbit camera** with photo mode;
  - **Star Bit helpers**: collect every Star Bit on screen, or shoot the nearest enemy;
  - **Generic character**, **Generic world** and **Hide HUD**: a plain figure, plain
    terrain and objects, and no on-screen counters, for clean footage;
  - a **mod folder** for replacing or adding disc files.

> [!IMPORTANT]
> **This is an unofficial, independent project, not affiliated with the Petari project or its
> Discord, and not with Nintendo.** The Petari decompilation itself is not meant
> to be a PC port: please do **not** ask the Petari maintainers or their Discord
> about this port. Report problems here instead.
>
> **No game data is included.** You need your own copy of the game, extracted
> from a disc you own. This repository contains no Nintendo assets and no
> download links for them.
>
> Most of the native port (everything under `native/` and the `PETARI_NATIVE`
> hooks) was written by AI models under the repository owner's direction (see
> [Acknowledgments](#acknowledgments)). It is not decompilation work and is not
> intended for submission upstream.

The game is playable but this is still a development build: expect bugs, and
see [Known issues](#known-issues).

## Requirements

- A Mac with Apple Silicon (M1 or newer). Intel Macs are not supported.
- **Xcode 26 or newer** (or its Command Line Tools). Aurora uses C++20
  `std::jthread` and `std::stop_token`, which Apple's libc++ only has from Xcode
  26: the build works with Xcode 26.4 (Apple clang 21) and fails with Xcode 16.4
  (Apple clang 17). Versions in between are untested. Check with
  `clang --version`; switch with `sudo xcode-select -s /Applications/Xcode.app`.
- Developed on macOS 27. The app is built for the macOS version of your SDK (no
  older deployment target has been tested).
- [CMake](https://cmake.org/) 3.25 or newer, and SQLite 3.37 or newer
  (Homebrew: `brew install cmake sqlite`).
- An internet connection for the first configure: CMake downloads Aurora and its
  dependencies (SDL3, Dawn, Dear ImGui, Tracy, xxHash) at pinned versions.
- About 10 GB of disk space for the build, plus the extracted game.

## Your game data

MacioGalaxy needs the **USA release of Super Mario Galaxy, revision 0 (game ID
`RMGE01`)**. Other regions and revisions have not been tested.

Make a backup of your own disc (for example with a Wii and a disc-dumping
homebrew tool), then extract its data partition:

- [Dolphin](https://dolphin-emu.org/): right-click the game, **Properties >
  Filesystem**, right-click the **Data Partition**, **Extract Entire Partition**.
- Or Wiimms ISO Tools: `wit extract <your-image> build/game-data/RMGE01`.

Put the result at `build/game-data/RMGE01` in this repository (or anywhere,
and point `PETARI_GAME_DIR` at it). Either layout works:

```
build/game-data/RMGE01/
  files/          the game's files (StageData/, ObjectData/, AudioRes/, ...)
  sys/            boot.bin, fst.bin, ... (optional, but used when present)
```

or Dolphin's `build/game-data/RMGE01/DATA/files` and `DATA/sys`.

## Build

```sh
git clone https://github.com/classicrob/MacioGalaxy.git
cd MacioGalaxy
```

From the repository root:

```sh
brew install cmake sqlite
cmake -S . -B build/macos-gx \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DPETARI_BUILD_GX_PROBE=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix sqlite)"
cmake --build build/macos-gx --target petari -j 8
```

The first build compiles the whole game and takes a while. To run the native
test suite as well (no game data needed for most tests):

```sh
cmake --build build/macos-gx -j 8
cmake --build build/macos-gx --target petari_tests -j 8
ctest --test-dir build/macos-gx -j 4
```

(The app and some test executables are not part of the default `all` target;
`--target petari` and `--target petari_tests` build them.)

Tests labelled `timing` check real-time behaviour (thread hand-off latency,
audio pacing, the audio thread during shader compiles). They need a quiet,
real Mac: run them alone with `ctest --test-dir build/macos-gx -L timing`, and
skip them on shared or virtual machines with `-LE timing` (CI does).
More build detail, including the sanitizer core-library presets, is in
[native/README.md](native/README.md).

## First launch

Double-click **`Play MacioGalaxy.command`** in the repository root, or run:

```sh
build/macos-gx/native/app/Petari.app/Contents/MacOS/Petari --disc build/game-data/RMGE01
```

- There is no preparation screen: the game starts straight away. The first
  visit to each new area compiles Metal shaders, so it can hitch briefly. The
  shader cache is kept in `~/Library/Caches/Petari`, so later visits are smooth.
  A fresh clone starts with the tracked shader seed: configuring (`cmake` or
  the build presets) expands `native/data/pipeline-seeds.v13.xz` into
  `build/pipeline-seeds`, and the app bundles it, so the common GX pipelines are
  prepared up front. The seed holds render-state configs only (no shaders,
  textures or disc data). Without it the game still runs; it just hitches more
  on first visits. See
  [native/tools/PIPELINE_SEEDING.md](native/tools/PIPELINE_SEEDING.md#repacking-the-seed-pack).
- Saves, settings (`controls.txt`, `mods.txt`, `sound.txt`) and crash reports live in
  `~/Library/Application Support/Petari`. Use `--user DIR` for a separate save
  location. Native saves are not compatible with Wii saves.
- At the title screen press **Return** (the game's "press A and B").
- To go straight into a galaxy after loading your file, add `--stage` and
  `--scenario`, e.g. `./Play\ MacioGalaxy.command --stage good-egg --scenario 2`.
  Names, missions and rules: [native/LEVEL_SELECT.md](native/LEVEL_SELECT.md).

## Controls

Keyboard and mouse by default, with game controller support. The main keys:
**WASD** move, **Space** jump, **F** spin, **Shift** crouch, mouse aims the Star
Pointer, left click shoots Star Bits, **Q/E** rotate the camera, **Escape**
pauses, and **`** (the key under Escape) or **F1** opens the native Home menu
(Controls, Mods, a Volume slider: Left/Right in 5% steps or click and drag it,
Restart, Quit). Everything is listed, and remappable, in
[native/CONTROLS.md](native/CONTROLS.md). Controller support follows SDL3's
standard layout but has not yet been tested with physical hardware.

## Saves

Unlocked saves (the observatory restored, so you can jump straight to later
galaxies) are made from your own save file by a script, through the game's own
save code. How to make one, install it, and restore your previous save with a
backup: [native/SAVES.md](native/SAVES.md).

## Mods

Optional gameplay helpers, all **off by default**; turn them on in the Home
menu (**`** or **F1**, then **Mods**) or in `mods.txt`. With every mod off the
game behaves as the original.

- **Skip cutscenes and dialogue** (hold **Space** 1.5 s), **Point to move** (hold
  the left mouse button), **Odyssey movement**, **Cappy** (**R**), the **Odyssey
  camera** and photo mode, **Collect Star Bits** (**G**), **Shoot nearest enemy**
  (**V**), **Generic character**, **Generic world**, **Hide the HUD**, and a
  mod folder for disc-file mods.

What each does, its keys and how it was checked: [native/MODS.md](native/MODS.md).

## Known issues

- This is a development build. Automated testing enters every one of the 135
  galaxy missions and checks that each loads and plays (move, jump, spin,
  camera, pause) without a crash, hang or renderer error; every observatory
  dome has been toured from unlocked saves; and full missions have been played
  to the Power Star by hand. Not every mission has been played to completion,
  and the ending sequence has not been played through.
- The first time a Mac runs the game it compiles shaders in the background, so
  the first few minutes can stutter and objects may pop in briefly. After that
  the shader cache in `~/Library/Caches/Petari` makes launches immediate and
  smooth, including with other save files.
- Physical game controllers have not been tested yet.
- Visual fidelity is close but not yet systematically compared with the Wii;
  some effects may differ.
- The original small UI textures are low resolution by design.
- Report bugs in this repository's issues with what you did and the log
  printed in Terminal. Do not report them to the Petari project.

Detailed test coverage: [native/RELIABILITY.md](native/RELIABILITY.md) and
[native/PLAYTEST.md](native/PLAYTEST.md).

## Built on Petari

[**Petari**](https://github.com/SMGCommunity/Petari) is the SMGCommunity's
decompilation of Super Mario Galaxy: human-written C++ that compiles to a
byte-matching build of the original Wii game. MacioGalaxy compiles that
source for arm64 (the `PETARI_NATIVE` hooks in `src/` and everything in
`native/` are this project's work) instead of for the Wii. Petari's own README
covers the decompilation, its matching Wii build, its progress tracking and its
contribution guide; its work and Discord are for the decompilation, not for
this port. **Please do not ask the Petari maintainers about MacioGalaxy**:
report problems in this repository.

## Credits and licenses

- **Petari decompilation**: the [SMGCommunity/Petari](https://github.com/SMGCommunity/Petari)
  contributors. Released under CC0 1.0 ([LICENSE](LICENSE)). Petari in turn
  sourced code and headers from the
  [doldecomp](https://github.com/doldecomp/sdk_2009-12-11) SDK decompilation,
  [zeldaret/tp](https://github.com/zeldaret/tp) (JSystem) and
  [doldecomp/ogws](https://github.com/doldecomp/ogws) (nw4r).
- **This port's own code** (the native port, its tools and documentation) is
  also released under **CC0 1.0**, the same terms as the decompilation: you can
  use it for any purpose without asking. The dependencies below keep their own
  licenses.
- **Aurora** by Luke Street ([encounter/aurora](https://github.com/encounter/aurora)),
  the GX-to-WebGPU/Metal backend: MIT License, Copyright (c) 2022 Luke Street.
- **Dawn** ([dawn.googlesource.com](https://dawn.googlesource.com/dawn)), the
  WebGPU implementation used by Aurora, fetched as a prebuilt package: BSD
  3-Clause License, Copyright 2017-2023 The Dawn & Tint Authors.
- **SDL3** ([libsdl.org](https://www.libsdl.org/)): zlib License,
  Copyright (C) 1997-2026 Sam Lantinga.
- **Dear ImGui** ([ocornut/imgui](https://github.com/ocornut/imgui)): MIT License,
  Copyright (c) 2014-2025 Omar Cornut.
- **Tracy** ([wolfpld/tracy](https://github.com/wolfpld/tracy)): BSD 3-Clause License.
- **FreeType** ([freetype.org](https://freetype.org/)): used by Dear ImGui's font
  builder; found on the system (Homebrew), not vendored. FreeType License or GPLv2.
- **xxHash** ([Cyan4973/xxHash](https://github.com/Cyan4973/xxHash)): BSD 2-Clause
  License, Copyright (c) 2012-2021 Yann Collet.
- **SQLite**: public domain.
- **Super Mario Odyssey data for the Odyssey mods**: player constants from
  [MonsterDruide1/OdysseyDecomp](https://github.com/MonsterDruide1/OdysseyDecomp)
  (v1.0.0, used as numbers for the movement and Cappy mods), and measurements from
  [smo.wiki](https://smo.wiki/) and the "Odyssey Movement Data v2" sheet. Details in
  `docs/dev/ODYSSEY_MOVEMENT.md`; no Nintendo assets or code are included.

These dependencies are downloaded at build time, not included in this
repository; their license files come with their sources (Dawn's prebuilt
package does not include one; see the Dawn repository). If you distribute a
built app, include those licenses with it.

## Acknowledgments

Most of this port was written by AI models, working under the direction of the
repository owner ([classicrob](https://github.com/classicrob)), who chose the
goals, played the builds and reviewed the results:

- **Claude**, by Anthropic, used through Claude Code.
- **Astra**, an OpenAI model, used through Codex.

Many of their commits carry `Co-Authored-By` trailers. The port builds on
the work of the Petari decompilation contributors and on Aurora; without them
MacioGalaxy would not exist.

Super Mario Galaxy is a trademark of Nintendo. This project is not affiliated
with or endorsed by Nintendo.
