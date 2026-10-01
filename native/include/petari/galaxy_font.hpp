#pragma once
// The game's own fonts, read from the player's disc for the native menus.
//
// LayoutData/Font.arc (Yaz0-compressed RARC) holds the fonts the game's layouts
// and messages use, as NW4R binary fonts (.brfnt, "RFNT"): GX texture sheets of
// glyph cells, per-glyph widths (CWDH) and character maps (CMAP). This decodes
// them into straight-alpha RGBA bitmaps, one per glyph, that the Dear ImGui
// overlay adds to its font atlas (imgui_overlay.cpp). Nothing here depends on
// ImGui or the SDK, and nothing from the disc is stored in the repository.
//
// The fonts used:
// - MessageFont26: the game's message and menu text (I4, white).
// - MenuFont64: the large outlined title lettering (IA4: white fill, black outline).
// - PictureFont: the colored icons messages embed (RGB5A3): the Wii Remote's
//   buttons, the Nunchuk, the Control Stick, the + Control Pad, stars, coins.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace PetariNative::HomeMenu::GalaxyFont {

struct Glyph {
    std::uint32_t code = 0;  // the font's character code (UTF-16 for these fonts)
    int left = 0;            // pen to the bitmap's left edge, pixels
    int advance = 0;         // pen advance
    int width = 0;           // bitmap: the glyph's width of its cell...
    int height = 0;          // ...and the cell height
    std::vector<std::uint8_t> rgba;  // width * height straight-alpha RGBA8, rows top down
};

struct Font {
    int lineFeed = 0;    // line height
    int ascent = 0;
    int baseline = 0;    // cell top to baseline
    int cellWidth = 0;
    int cellHeight = 0;
    std::vector<Glyph> glyphs;  // ascending code

    const Glyph* find(std::uint32_t code) const;
};

// Decompresses a Yaz0 stream (16-byte header, then the payload). False if the
// data is not Yaz0 or is truncated.
bool yaz0Decompress(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>* out);

// A file of a RARC archive by name, at any depth, ignoring case. False if the
// archive is malformed or has no such file.
bool rarcFile(const std::vector<std::uint8_t>& archive, const std::string& name, std::vector<std::uint8_t>* out);

// GX texture formats the fonts use; anything else fails parseRfnt.
enum class TexFormat : std::uint16_t { I4 = 0, I8 = 1, IA4 = 2, IA8 = 3, RGB565 = 4, RGB5A3 = 5 };
// Decodes one tiled GX image to RGBA8 (width * height * 4). False for an
// unsupported format or too little data. Intensity formats decode to white
// (I4/I8: alpha is the intensity; IA4/IA8: grey level with its alpha).
bool decodeTexture(TexFormat format, int width, int height, const std::uint8_t* data, std::size_t size,
                   std::vector<std::uint8_t>* rgba);

// Parses an RFNT, keeping the glyphs whose code `want` accepts (all if empty).
bool parseRfnt(const std::vector<std::uint8_t>& file, const std::function<bool(std::uint32_t)>& want, Font* out,
               std::string* error);

struct Set {
    Font message;  // MessageFont26
    Font menu;     // MenuFont64
    Font picture;  // PictureFont
};

// Reads <disc>/files/LayoutData/Font.arc (or the given .arc), keeping the
// characters the menus can show: ASCII, Latin-1 and common punctuation and
// arrows for the message font, ASCII and Latin-1 for the title font, every
// picture. False, with a message, if the file is missing or not as expected.
bool load(const std::filesystem::path& fontArchive, Set* out, std::string* error);

// The picture font's icons, by their character code there (PictureFont's
// CMAP maps '0'.. to its cells). The overlay exposes picture glyph `c` as
// U+E000 + (c - 0x20), so strings can embed them (see kIcon* in home_menu.hpp).
namespace Picture {
constexpr std::uint32_t A = '0';
constexpr std::uint32_t B = '1';
constexpr std::uint32_t C = '2';
constexpr std::uint32_t Remote = '3';
constexpr std::uint32_t Nunchuk = '4';
constexpr std::uint32_t One = '5';
constexpr std::uint32_t Two = '6';
constexpr std::uint32_t Pointer = '7';  // the Star Pointer
constexpr std::uint32_t Stick = '?';    // the Nunchuk's Control Stick
constexpr std::uint32_t Coin = 'A';
constexpr std::uint32_t Mario = 'B';
constexpr std::uint32_t Pad = 'C';       // + Control Pad
constexpr std::uint32_t Home = 'F';
constexpr std::uint32_t Minus = 'G';
constexpr std::uint32_t Plus = 'H';
constexpr std::uint32_t Z = 'I';
constexpr std::uint32_t PowerStar = '8';
}  // namespace Picture

}  // namespace PetariNative::HomeMenu::GalaxyFont
