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

- The paused game is tinted deep teal, as the game's pause screen tints it.
- Panels: a dark space-blue gradient with a cyan rim and a faint inner line.
- Buttons: the game's white-to-blue capsules with a cyan rim; the focused
  one turns gold and glows.
- Text has a soft drop shadow; titles are white MenuFont64.
- A footer band along the bottom of the image names the buttons that work on
  the page, with their icons and current keys ("(A) Space: choose  (+)
  Escape: back  (HOME) F1: close"; Level Select, Odyssey camera and My
  Progress add the + Control Pad's "change").
- Sizes follow the game image's height, so the pages read the same at any
  window size; long messages wrap to two lines.

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

## Tests

`native_galaxy_font` (`native/tests/galaxy_font_tests.cpp`): Yaz0, RARC,
texture and RFNT decoding on synthetic data (including truncated and
malformed input), the ImGui atlas with the fonts installed (letters white,
icons in color, ImGui's own font kept first), a missing archive leaving the
built-in font, and, when the disc is present, the real Font.arc.
`native_home_menu` checks the overlay's geometry, colors and readability at
480 lines with the built-in font.
