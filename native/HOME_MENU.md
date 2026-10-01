# Home menu look

The native Home menu (F1 or `; `native/home_menu/`) draws with the game's own
lettering and button icons, styled after the game's pause screen.

## Fonts from the disc

At startup the app reads `files/LayoutData/Font.arc` from the player's disc
(`petari/galaxy_font.hpp`): a Yaz0-compressed RARC of NW4R fonts (`.brfnt`).
The glyph sheets are decoded (I4, IA4, RGB5A3 and the other GX formats the
fonts use) and added to the Dear ImGui font atlas (`installGalaxyFonts`):

| Font | Used for |
| --- | --- |
| MessageFont26 | All menu text: messages, buttons, the controls table, the footer, the title hint, the My Progress badges |
| MenuFont64 | Page titles (the outlined lettering of the game's banners) |
| PictureFont | Colored icons embedded in text: the A, B, C, Z, +, -, 1, 2 and HOME buttons, the + Control Pad, the Control Stick, the Wii Remote |

Picture character `c` is exposed as U+E000 + (c - 0x20); `home_menu.hpp`
has the UTF-8 constants (`PETARI_ICON_A` and so on). Icons keep their own
colors in tinted text.

Nothing from the disc is stored in the repository. Without the file (no disc,
CI) the app logs `petari: menus use the built-in font: ...` and the menus use
ImGui's built-in font, with icons shown as `(A)`, `(+)` and so on. The layout
is the same either way.

## Style

- The main list and the Restart/Quit confirmations use the game's pause-screen
  layout: translucent teal bands across the top (title and message) and the
  bottom (the footer), with the buttons over the scene between them.
- The other pages dim the game in deep teal and sit on a panel: a teal
  gradient with a cyan rim and a faint inner line.
- Buttons: the game's white-to-blue capsules with a cyan rim; the focused
  one turns gold and glows.
- Text has a soft drop shadow; titles are white MenuFont64.
- A footer band along the bottom of the image names the buttons that work on
  the page, with their icons and current keys ("(A) Space: choose  (+)
  Escape: back  (HOME) F1: close"; Level Select, Odyssey camera and My
  Progress add the + Control Pad's "change").
- Sizes follow the game image's height, so the pages read the same at any
  window size; long messages wrap to two lines.

## Controls page

The Controls page is a remapper (native/CONTROLS.md "Changing controls"): a
scrolling table of every action, with its icon, two keyboard/mouse slots and
two controller slots as key caps, and a scrollbar on the right (arrow
buttons, a track that pages when clicked, and a draggable capsule thumb sized
to the shown fraction). The focused slot is a gold capsule that
pulses "Press..." while it waits for an input. Shared inputs are outlined in
orange, and changed actions are named in gold. The buttons below are Clear
slot, Reset action, Reset all and Back, or Swap, Keep both and Cancel for a
conflict. The page logic is in `home_menu.cpp` (`Menu::updateRemap`) on a
`ControlsBackend`. The bridge's backend (`hbm_bridge.cpp`, `InputControls`)
applies changes to the live bindings and saves `controls.txt`.

## Checking the look

- Offline, every page to PNG (no game, any size):
  `cmake --build build/macos-gx --target petari_home_menu_render`, then
  `build/macos-gx/native/home_menu/petari_home_menu_render <out-dir> --font
  build/game-data/RMGE01/files/LayoutData/Font.arc [--background bg.ppm]
  [--size 1280x720] [--wide 0|1]`. It rasterizes ImGui's draw data in
  software.
- In the game: the stage smoke with `PETARI_STAGE_HOME_TOUR=1` opens the
  Home menu after its pause checks and visits every page, marking
  `home-<page>`; with `PETARI_SURFACE_DUMP=<dir>
  PETARI_SURFACE_DUMP_LABELS=home-,pause` the window images (overlay
  included) are written there. The game's own pause menu is marked `pause`.
  On Controls the tour remaps Cap throw to F (Spin's key) through a real
  capture and chooses Swap (`home-remap-capture`, `-conflict`, `-done`), so
  the run's `controls.txt` ends with `ModCappyThrow=Key:F,...` and
  `Shake=Key:R,...`. Before that it scrolls the list with two wheel notches
  (`home-scroll-wheel`) and drags the scrollbar's thumb to the bottom with the
  left mouse button (`home-scroll-drag`).

## Tests

`native_galaxy_font` (`native/tests/galaxy_font_tests.cpp`): Yaz0, RARC,
texture and RFNT decoding on synthetic data (including truncated and
malformed input), the ImGui atlas with the fonts installed (letters white,
icons in color, ImGui's own font kept first), a missing archive leaving the
built-in font, and, when the disc is present, the real Font.arc.
Both also run in the standalone build (`cmake -S native/home_menu -B
build/home-menu -DPETARI_SANITIZERS=ON`), which uses ImGui's stb_truetype
builder instead of FreeType. `native_home_menu` checks the overlay's geometry, colors and readability at
480 lines with the built-in font, and the remapper on a fake backend:
navigation and scrolling (the wheel, pages, LB/LT, the scrollbar's arrows,
track and thumb drag, clicks on the panel's empty space), capture, the conflict choices, Escape and the
10-second timeout, the refused Home change, the buttons (Reset all needs a
second A), the pointer, and a black-out cancelling a capture.
