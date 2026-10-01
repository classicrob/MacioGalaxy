// The game's fonts for the native menus (petari/galaxy_font.hpp): Yaz0, RARC,
// GX texture and RFNT decoding on synthetic data, the ImGui atlas with the
// fonts installed, and, when the disc is present, the real Font.arc.

#include "petari/galaxy_font.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef PETARI_GALAXY_FONT_TEST_IMGUI
#include <imgui.h>

#include "petari/home_menu.hpp"
#endif

namespace GF = PetariNative::HomeMenu::GalaxyFont;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const std::string& message) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    }
}

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, size_t at, std::uint16_t v) {
    if (b.size() < at + 2) b.resize(at + 2);
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}

void put32(Bytes& b, size_t at, std::uint32_t v) {
    put16(b, at, static_cast<std::uint16_t>(v >> 16));
    put16(b, at + 2, static_cast<std::uint16_t>(v));
}

Bytes yaz0(std::uint32_t length, const Bytes& payload) {
    Bytes b = {'Y', 'a', 'z', '0'};
    put32(b, 4, length);
    b.resize(16, 0);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

void testYaz0() {
    Bytes out;
    // Four literals, then a short back-reference: distance 4, length 8.
    const Bytes shortRef = yaz0(12, {0xF0, 'A', 'B', 'C', 'D', 0x60, 0x03});
    check(GF::yaz0Decompress(shortRef.data(), shortRef.size(), &out) &&
              std::string(out.begin(), out.end()) == "ABCDABCDABCD",
          "Yaz0 literals and a short copy");
    // A long copy: length 0x12 + 5.
    const Bytes longRef = yaz0(27, {0xF0, 'A', 'B', 'C', 'D', 0x00, 0x03, 0x05});
    check(GF::yaz0Decompress(longRef.data(), longRef.size(), &out) && out.size() == 27 && out[26] == 'C',
          "Yaz0 long copy");
    // Overlapping copy (distance 1) repeats one byte.
    const Bytes run = yaz0(6, {0x80, 'x', 0x30, 0x00});
    check(GF::yaz0Decompress(run.data(), run.size(), &out) && std::string(out.begin(), out.end()) == "xxxxxx",
          "Yaz0 overlapping copy");
    const Bytes truncated = yaz0(12, {0xF0, 'A', 'B'});
    check(!GF::yaz0Decompress(truncated.data(), truncated.size(), &out), "truncated Yaz0 fails");
    const Bytes before = yaz0(4, {0x00, 0x00, 0x05});  // copies from before the start
    check(!GF::yaz0Decompress(before.data(), before.size(), &out), "Yaz0 copy before the output fails");
    const Bytes notYaz0 = {'R', 'A', 'R', 'C'};
    check(!GF::yaz0Decompress(notYaz0.data(), notYaz0.size(), &out), "non-Yaz0 data fails");
}

// RARC: one root node, entries "Hello.bin", ".", "..".
Bytes rarc() {
    Bytes b(0xC5, 0);
    std::memcpy(b.data(), "RARC", 4);
    put32(b, 0x04, 0xC5);
    put32(b, 0x08, 0x20);
    put32(b, 0x0C, 0xA0);  // data at 0x20 + 0xA0
    put32(b, 0x10, 5);
    put32(b, 0x20, 1);     // nodes
    put32(b, 0x24, 0x20);  // node at 0x40
    put32(b, 0x28, 3);     // entries
    put32(b, 0x2C, 0x30);  // at 0x50
    put32(b, 0x30, 0x20);  // strings
    put32(b, 0x34, 0x80);  // at 0xA0
    const char strings[] = ".\0..\0Hello.bin";
    std::memcpy(b.data() + 0xA0, strings, sizeof(strings));
    auto entry = [&](int i, std::uint8_t flags, std::uint16_t name, std::uint32_t offset, std::uint32_t size) {
        const size_t at = 0x50 + i * 20;
        put16(b, at, static_cast<std::uint16_t>(i));
        b[at + 4] = flags;
        put16(b, at + 6, name);
        put32(b, at + 8, offset);
        put32(b, at + 12, size);
    };
    entry(0, 0x11, 5, 0, 5);  // Hello.bin
    entry(1, 0x02, 0, 0, 0);  // .
    entry(2, 0x02, 2, 0, 0);  // ..
    std::memcpy(b.data() + 0xC0, "hello", 5);
    return b;
}

void testRarc() {
    const Bytes archive = rarc();
    Bytes file;
    check(GF::rarcFile(archive, "hello.BIN", &file) && std::string(file.begin(), file.end()) == "hello",
          "RARC file found ignoring case");
    check(GF::rarcFile(archive, "/Hello.bin", &file), "RARC name with a leading slash");
    check(!GF::rarcFile(archive, "..", &file), "directories are not files");
    check(!GF::rarcFile(archive, "missing.bin", &file), "missing RARC file");
    Bytes cut(archive.begin(), archive.begin() + 0x60);
    check(!GF::rarcFile(cut, "Hello.bin", &file), "truncated RARC fails");
    Bytes bad = archive;
    put32(bad, 0x50 + 12, 0x1000);  // size past the end
    check(!GF::rarcFile(bad, "Hello.bin", &file), "RARC file past the end fails");
}

const std::uint8_t* pixel(const Bytes& rgba, int width, int x, int y) {
    return rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
}

void testTextures() {
    Bytes rgba;
    // I4, one 8x8 block: the first byte holds pixels (0,0) and (1,0); row 1 starts at byte 4.
    Bytes i4(32, 0);
    i4[0] = 0xF0;
    i4[4] = 0x08;
    check(GF::decodeTexture(GF::TexFormat::I4, 8, 8, i4.data(), i4.size(), &rgba), "I4 decodes");
    check(pixel(rgba, 8, 0, 0)[3] == 255 && pixel(rgba, 8, 1, 0)[3] == 0 && pixel(rgba, 8, 1, 1)[3] == 136 &&
              pixel(rgba, 8, 0, 0)[0] == 255,
          "I4 is white with the intensity as alpha, in 8x8 tiles");
    // Two I4 blocks side by side: the second block's first pixel is (8,0).
    Bytes i4b(64, 0);
    i4b[32] = 0xF0;
    check(GF::decodeTexture(GF::TexFormat::I4, 16, 8, i4b.data(), i4b.size(), &rgba) && pixel(rgba, 16, 8, 0)[3] == 255 &&
              pixel(rgba, 16, 0, 0)[3] == 0,
          "I4 tile order");
    // IA4, 8x4 tiles: high nibble alpha, low nibble intensity.
    Bytes ia4(32, 0);
    ia4[0] = 0xF8;
    check(GF::decodeTexture(GF::TexFormat::IA4, 8, 4, ia4.data(), ia4.size(), &rgba) && pixel(rgba, 8, 0, 0)[3] == 255 &&
              pixel(rgba, 8, 0, 0)[0] == 136,
          "IA4 alpha and grey level");
    // RGB5A3, 4x4 tiles: opaque 555, or 3-bit alpha and 444.
    Bytes rgb5a3(32, 0);
    put16(rgb5a3, 0, 0xFC00);  // opaque red
    put16(rgb5a3, 2, 0x3F0F);  // alpha 3/7, red 15, blue 15
    check(GF::decodeTexture(GF::TexFormat::RGB5A3, 4, 4, rgb5a3.data(), rgb5a3.size(), &rgba), "RGB5A3 decodes");
    const std::uint8_t* red = pixel(rgba, 4, 0, 0);
    const std::uint8_t* mixed = pixel(rgba, 4, 1, 0);
    check(red[0] == 255 && red[1] == 0 && red[2] == 0 && red[3] == 255, "RGB5A3 opaque color");
    check(mixed[0] == 255 && mixed[1] == 0 && mixed[2] == 255 && mixed[3] == 109, "RGB5A3 translucent color");
    check(!GF::decodeTexture(GF::TexFormat::I4, 8, 8, i4.data(), 16, &rgba), "too little texture data fails");
    check(!GF::decodeTexture(static_cast<GF::TexFormat>(14), 8, 8, i4.data(), i4.size(), &rgba), "unknown format fails");
}

// An RFNT with 3x3 cells (4x4 with the spacing) on one 8x8 I4 sheet, two per
// row: glyph 0 at (0,0), 1 at (4,0), 2 at (0,4), 3 at (4,4). Maps: 'A'..'B'
// direct (0, 1), 'a'..'b' by table (2, none), 'z' by scan (3).
Bytes rfnt() {
    Bytes b(0xEC, 0);
    std::memcpy(b.data(), "RFNT", 4);
    put16(b, 0x04, 0xFEFF);
    put16(b, 0x06, 0x0104);
    put32(b, 0x08, 0xEC);
    put16(b, 0x0C, 0x10);
    put16(b, 0x0E, 6);
    // FINF
    std::memcpy(b.data() + 0x10, "FINF", 4);
    put32(b, 0x14, 0x20);
    b[0x18] = 1;     // type
    b[0x19] = 4;     // line feed
    b[0x1C] = 0;     // default widths
    b[0x1D] = 3;
    b[0x1E] = 4;
    b[0x1F] = 1;     // UTF-16
    put32(b, 0x20, 0x38);  // TGLP data
    put32(b, 0x24, 0x88);  // CWDH data
    put32(b, 0x28, 0xA8);  // first CMAP data
    b[0x2E] = 3;           // ascent
    // TGLP
    std::memcpy(b.data() + 0x30, "TGLP", 4);
    put32(b, 0x34, 0x50);
    b[0x38] = 3;  // cell width
    b[0x39] = 3;  // cell height
    b[0x3A] = 2;  // baseline
    b[0x3B] = 3;
    put32(b, 0x3C, 32);  // sheet size
    put16(b, 0x40, 1);   // sheets
    put16(b, 0x42, 0);   // I4
    put16(b, 0x44, 2);   // per row
    put16(b, 0x46, 2);   // rows
    put16(b, 0x48, 8);
    put16(b, 0x4A, 8);
    put32(b, 0x4C, 0x60);
    // The sheet: glyph 1's top-left pixel (4,0) opaque; glyph 3's (4,4) half.
    b[0x60 + 2] = 0xFF;          // row 0, pixels 4 and 5
    b[0x60 + 4 * 4 + 2] = 0x80;  // row 4, pixel 4
    // CWDH: four glyphs.
    std::memcpy(b.data() + 0x80, "CWDH", 4);
    put32(b, 0x84, 0x20);
    put16(b, 0x88, 0);
    put16(b, 0x8A, 3);
    put32(b, 0x8C, 0);
    const std::int8_t widths[4][3] = {{0, 3, 4}, {1, 2, 3}, {-1, 3, 3}, {0, 1, 2}};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) b[0x90 + i * 3 + j] = static_cast<std::uint8_t>(widths[i][j]);
    }
    // CMAP direct
    std::memcpy(b.data() + 0xA0, "CMAP", 4);
    put32(b, 0xA4, 0x18);
    put16(b, 0xA8, 'A');
    put16(b, 0xAA, 'B');
    put16(b, 0xAC, 0);
    put32(b, 0xB0, 0xC0);
    put16(b, 0xB4, 0);
    // CMAP table
    std::memcpy(b.data() + 0xB8, "CMAP", 4);
    put32(b, 0xBC, 0x18);
    put16(b, 0xC0, 'a');
    put16(b, 0xC2, 'b');
    put16(b, 0xC4, 1);
    put32(b, 0xC8, 0xD8);
    put16(b, 0xCC, 2);
    put16(b, 0xCE, 0xFFFF);
    // CMAP scan
    std::memcpy(b.data() + 0xD0, "CMAP", 4);
    put32(b, 0xD4, 0x1C);
    put16(b, 0xD8, 0);
    put16(b, 0xDA, 0xFFFF);
    put16(b, 0xDC, 2);
    put32(b, 0xE0, 0);
    put16(b, 0xE4, 1);
    put16(b, 0xE6, 'z');
    put16(b, 0xE8, 3);
    return b;
}

void testRfnt() {
    const Bytes file = rfnt();
    GF::Font font;
    std::string error;
    check(GF::parseRfnt(file, {}, &font, &error), "RFNT parses: " + error);
    check(font.lineFeed == 4 && font.ascent == 3 && font.cellWidth == 3 && font.cellHeight == 3 && font.baseline == 2,
          "RFNT metrics");
    check(font.glyphs.size() == 4, "every mapped glyph (" + std::to_string(font.glyphs.size()) + ")");
    const GF::Glyph* a = font.find('A');
    const GF::Glyph* b = font.find('B');
    const GF::Glyph* lowerA = font.find('a');
    const GF::Glyph* z = font.find('z');
    check(a && b && lowerA && z && font.find('b') == nullptr, "direct, table and scan maps; an unmapped code");
    if (a && b && lowerA && z) {
        check(a->advance == 4 && a->width == 3 && a->left == 0, "glyph 0 widths");
        check(b->left == 1 && b->width == 2 && b->advance == 3, "glyph 1 widths");
        check(lowerA->left == -1, "negative left offset");
        check(b->rgba[3] == 255 && b->rgba[7] == 255 && a->rgba[3] == 0, "glyph 1's cell is at (4,0)");
        check(z->width == 1 && z->rgba[3] == 136, "glyph 3's cell is at (4,4)");
    }
    GF::Font filtered;
    check(GF::parseRfnt(file, [](std::uint32_t c) { return c == 'z'; }, &filtered, &error) && filtered.glyphs.size() == 1,
          "the filter keeps only wanted codes");
    check(!GF::parseRfnt(file, [](std::uint32_t) { return false; }, &filtered, &error), "no wanted glyph fails");
    for (size_t cut : {size_t{8}, size_t{0x30}, size_t{0x70}, size_t{0xC0}}) {
        GF::Font broken;
        check(!GF::parseRfnt(Bytes(file.begin(), file.begin() + cut), {}, &broken, &error),
              "truncated RFNT fails (" + std::to_string(cut) + " bytes)");
    }
    Bytes badMap = file;
    put32(badMap, 0x28, 0x1000);
    check(!GF::parseRfnt(badMap, {}, &font, &error), "character map out of range fails");
    Bytes compressed = file;
    put16(compressed, 0x42, 0x8000);
    check(!GF::parseRfnt(compressed, {}, &font, &error), "compressed sheets are refused");
}

void testMissing() {
    GF::Set set;
    std::string error;
    check(!GF::load("/nonexistent/Font.arc", &set, &error) && !error.empty(), "missing archive fails with a message");
    const std::filesystem::path junk = std::filesystem::temp_directory_path() / "petari-galaxy-font-junk.arc";
    {
        std::FILE* f = std::fopen(junk.c_str(), "wb");
        std::fputs("not an archive", f);
        std::fclose(f);
    }
    check(!GF::load(junk, &set, &error), "a junk file fails");
    std::filesystem::remove(junk);
}

#ifdef PETARI_GALAXY_FONT_TEST_IMGUI
void testImGui(const char* archive, bool haveDisc) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    std::string error;
    check(!PetariNative::HomeMenu::installGalaxyFonts("/nonexistent/Font.arc", &error) &&
              !PetariNative::HomeMenu::galaxyFontsInstalled(),
          "a missing archive leaves the built-in font");
    if (haveDisc) {
        check(PetariNative::HomeMenu::installGalaxyFonts(archive, &error) && PetariNative::HomeMenu::galaxyFontsInstalled(),
              "fonts install: " + error);
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        check(io.Fonts->Fonts.Size == 3, "ImGui's font, the message font and the title font");
        check(io.Fonts->Fonts[0]->FindGlyphNoFallback('A') != nullptr, "ImGui's own font stays first");
        ImFont* text = io.Fonts->Fonts[1];
        const ImFontGlyph* a = text->FindGlyphNoFallback('A');
        const ImFontGlyph* icon = text->FindGlyphNoFallback(static_cast<ImWchar>(0xE010));  // the A button
        check(a && icon && a->Visible && icon->Visible, "letters and the picture icons are glyphs");
        // Opaque white letter pixels, and colored icon pixels, in the atlas.
        auto count = [&](const ImFontGlyph* g, bool colored) {
            int n = 0;
            for (int y = static_cast<int>(g->V0 * height); y < static_cast<int>(g->V1 * height); ++y) {
                for (int x = static_cast<int>(g->U0 * width); x < static_cast<int>(g->U1 * width); ++x) {
                    const unsigned char* p = pixels + (static_cast<size_t>(y) * width + x) * 4;
                    const bool grey = p[0] == p[1] && p[1] == p[2];
                    n += p[3] > 200 && (colored ? !grey : (p[0] == 255 && grey)) ? 1 : 0;
                }
            }
            return n;
        };
        if (a && icon) {
            check(count(a, false) > 30, "the letter A is drawn in the atlas");
            check(count(icon, true) > 30, "the A button icon keeps its colors");
        }
        check(text->FontSize == 33.0f && io.Fonts->Fonts[2]->FontSize == 64.0f, "font sizes follow the line heights");
        // A second install is a no-op.
        check(PetariNative::HomeMenu::installGalaxyFonts(archive, &error) && io.Fonts->Fonts.Size == 3, "installed once");
    }
    ImGui::DestroyContext();
}
#endif

}  // namespace

int main() {
    testYaz0();
    testRarc();
    testTextures();
    testRfnt();
    testMissing();

    // The real fonts, when the disc is there (never in the repository).
    const char* archive = PETARI_TEST_FONT_ARCHIVE;
    const bool haveDisc = std::filesystem::exists(archive);
    if (haveDisc) {
        GF::Set set;
        std::string error;
        check(GF::load(archive, &set, &error), "the disc's fonts load: " + error);
        int ascii = 0;
        for (std::uint32_t c = 0x20; c < 0x7F; ++c) ascii += set.message.find(c) && set.menu.find(c) ? 1 : 0;
        check(ascii == 95, "every ASCII character in the message and title fonts (" + std::to_string(ascii) + ")");
        check(set.message.find(0xE9) && set.message.find(0x2192), "Latin-1 and arrows in the message font");
        const GF::Glyph* home = set.picture.find(GF::Picture::Home);
        check(home && home->width > 20 && home->height > 20, "the HOME button icon");
        check(set.message.lineFeed == 33 && set.menu.lineFeed == 64, "the fonts' line heights");
    } else {
        std::printf("galaxy font tests: no disc at %s; the disc checks are skipped\n", archive);
    }
#ifdef PETARI_GALAXY_FONT_TEST_IMGUI
    testImGui(archive, haveDisc);
#endif

    if (gFailures > 0) {
        std::fprintf(stderr, "%d of %d galaxy font checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("galaxy font tests passed (%d checks)\n", gChecks);
    return 0;
}
