// The game's fonts from the disc (petari/galaxy_font.hpp): Yaz0, RARC, RFNT and
// the GX texture formats the font sheets use. Everything is big-endian.

#include "petari/galaxy_font.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace PetariNative::HomeMenu::GalaxyFont {

namespace {

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] << 8 | p[1]);
}

std::uint32_t be32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
           static_cast<std::uint32_t>(p[2]) << 8 | p[3];
}

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// 4-bit and 3-bit channels to 8 bits.
std::uint8_t from4(unsigned v) { return static_cast<std::uint8_t>(v * 17); }
std::uint8_t from5(unsigned v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); }
std::uint8_t from6(unsigned v) { return static_cast<std::uint8_t>((v << 2) | (v >> 4)); }
std::uint8_t from3(unsigned v) { return static_cast<std::uint8_t>((v << 5) | (v << 2) | (v >> 1)); }

}  // namespace

const Glyph* Font::find(std::uint32_t code) const {
    const auto it = std::lower_bound(glyphs.begin(), glyphs.end(), code,
                                     [](const Glyph& glyph, std::uint32_t c) { return glyph.code < c; });
    return it != glyphs.end() && it->code == code ? &*it : nullptr;
}

bool yaz0Decompress(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>* out) {
    if (size < 16 || std::memcmp(data, "Yaz0", 4) != 0) {
        return false;
    }
    const std::uint32_t length = be32(data + 4);
    out->clear();
    out->reserve(length);
    std::size_t in = 16;
    while (out->size() < length) {
        if (in >= size) {
            return false;
        }
        const std::uint8_t code = data[in++];
        for (int bit = 7; bit >= 0 && out->size() < length; --bit) {
            if (code & (1u << bit)) {
                if (in >= size) {
                    return false;
                }
                out->push_back(data[in++]);
                continue;
            }
            if (in + 1 >= size) {
                return false;
            }
            const std::uint8_t b1 = data[in++];
            const std::uint8_t b2 = data[in++];
            const std::size_t distance = (static_cast<std::size_t>(b1 & 0x0F) << 8 | b2) + 1;
            std::size_t count = b1 >> 4;
            if (count == 0) {
                if (in >= size) {
                    return false;
                }
                count = data[in++] + 0x12u;
            } else {
                count += 2;
            }
            if (distance > out->size()) {
                return false;
            }
            for (std::size_t i = 0; i < count && out->size() < length; ++i) {
                out->push_back((*out)[out->size() - distance]);
            }
        }
    }
    return true;
}

bool rarcFile(const std::vector<std::uint8_t>& archive, const std::string& name, std::vector<std::uint8_t>* out) {
    const std::uint8_t* a = archive.data();
    const std::size_t size = archive.size();
    if (size < 0x40 || std::memcmp(a, "RARC", 4) != 0) {
        return false;
    }
    const std::size_t dataStart = static_cast<std::size_t>(be32(a + 0x0C)) + 0x20;
    const std::uint8_t* info = a + 0x20;
    const std::uint32_t entryCount = be32(info + 0x08);
    const std::size_t entries = static_cast<std::size_t>(be32(info + 0x0C)) + 0x20;
    const std::size_t stringsSize = be32(info + 0x10);
    const std::size_t strings = static_cast<std::size_t>(be32(info + 0x14)) + 0x20;
    if (entries + static_cast<std::size_t>(entryCount) * 20 > size || strings + stringsSize > size) {
        return false;
    }
    const std::string wanted = lower(name[0] == '/' ? name.substr(1) : name);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::uint8_t* e = a + entries + static_cast<std::size_t>(i) * 20;
        const std::uint8_t flags = e[4];
        if ((flags & 0x01) == 0) {
            continue;  // a directory
        }
        const std::size_t nameOffset = be16(e + 6);
        if (nameOffset >= stringsSize) {
            return false;
        }
        const char* entryName = reinterpret_cast<const char*>(a + strings + nameOffset);
        const std::size_t nameLength = strnlen(entryName, stringsSize - nameOffset);
        if (lower(std::string(entryName, nameLength)) != wanted) {
            continue;
        }
        const std::size_t offset = dataStart + be32(e + 8);
        const std::size_t length = be32(e + 12);
        if (offset > size || length > size - offset) {
            return false;
        }
        out->assign(a + offset, a + offset + length);
        return true;
    }
    return false;
}

bool decodeTexture(TexFormat format, int width, int height, const std::uint8_t* data, std::size_t size,
                   std::vector<std::uint8_t>* rgba) {
    int blockW = 4, blockH = 4, bitsPerPixel = 16;
    switch (format) {
    case TexFormat::I4: blockW = 8; blockH = 8; bitsPerPixel = 4; break;
    case TexFormat::I8: blockW = 8; blockH = 4; bitsPerPixel = 8; break;
    case TexFormat::IA4: blockW = 8; blockH = 4; bitsPerPixel = 8; break;
    case TexFormat::IA8: break;
    case TexFormat::RGB565: break;
    case TexFormat::RGB5A3: break;
    default: return false;
    }
    if (width <= 0 || height <= 0 || width % blockW != 0 || height % blockH != 0 ||
        static_cast<std::size_t>(width) * height * bitsPerPixel / 8 > size) {
        return false;
    }
    rgba->assign(static_cast<std::size_t>(width) * height * 4, 0);
    std::size_t pixel = 0;  // in data order
    for (int by = 0; by < height; by += blockH) {
        for (int bx = 0; bx < width; bx += blockW) {
            for (int y = 0; y < blockH; ++y) {
                for (int x = 0; x < blockW; ++x, ++pixel) {
                    std::uint8_t r = 255, g = 255, b = 255, alpha = 255;
                    switch (format) {
                    case TexFormat::I4: {
                        const std::uint8_t v = data[pixel / 2];
                        alpha = from4(pixel % 2 == 0 ? v >> 4 : v & 0x0F);
                        break;
                    }
                    case TexFormat::I8: alpha = data[pixel]; break;
                    case TexFormat::IA4: {
                        const std::uint8_t v = data[pixel];
                        alpha = from4(v >> 4);
                        r = g = b = from4(v & 0x0F);
                        break;
                    }
                    case TexFormat::IA8:
                        alpha = data[pixel * 2];
                        r = g = b = data[pixel * 2 + 1];
                        break;
                    case TexFormat::RGB565: {
                        const std::uint16_t v = be16(data + pixel * 2);
                        r = from5(v >> 11);
                        g = from6((v >> 5) & 0x3F);
                        b = from5(v & 0x1F);
                        break;
                    }
                    case TexFormat::RGB5A3: {
                        const std::uint16_t v = be16(data + pixel * 2);
                        if (v & 0x8000) {
                            r = from5((v >> 10) & 0x1F);
                            g = from5((v >> 5) & 0x1F);
                            b = from5(v & 0x1F);
                        } else {
                            alpha = from3((v >> 12) & 0x07);
                            r = from4((v >> 8) & 0x0F);
                            g = from4((v >> 4) & 0x0F);
                            b = from4(v & 0x0F);
                        }
                        break;
                    }
                    }
                    std::uint8_t* out = rgba->data() + (static_cast<std::size_t>(by + y) * width + bx + x) * 4;
                    out[0] = r;
                    out[1] = g;
                    out[2] = b;
                    out[3] = alpha;
                }
            }
        }
    }
    return true;
}

bool parseRfnt(const std::vector<std::uint8_t>& file, const std::function<bool(std::uint32_t)>& want, Font* out,
               std::string* error) {
    const std::uint8_t* f = file.data();
    const std::size_t size = file.size();
    if (size < 0x10 || std::memcmp(f, "RFNT", 4) != 0 || be16(f + 4) != 0xFEFF) {
        return fail(error, "not a big-endian RFNT font");
    }
    // Blocks follow the header: FINF, TGLP, CWDH..., CMAP...
    std::size_t finf = 0, tglp = 0;
    std::size_t at = be16(f + 0x0C);
    const int blocks = be16(f + 0x0E);
    for (int i = 0; i < blocks && at + 8 <= size; ++i) {
        const std::uint32_t blockSize = be32(f + at + 4);
        if (std::memcmp(f + at, "FINF", 4) == 0) finf = at;
        if (std::memcmp(f + at, "TGLP", 4) == 0) tglp = at;
        if (blockSize < 8) break;
        at += blockSize;
    }
    if (finf == 0 || tglp == 0 || finf + 0x20 > size || tglp + 0x20 > size) {
        return fail(error, "font without FINF or TGLP");
    }
    const std::uint8_t* info = f + finf + 8;
    const std::uint16_t alternate = be16(info + 2);
    const std::uint32_t widthsAt = be32(info + 0x0C);
    const std::uint32_t mapsAt = be32(info + 0x10);
    out->lineFeed = static_cast<std::int8_t>(info[1]);
    out->ascent = info[0x16];
    (void)alternate;

    const std::uint8_t* glyphs = f + tglp + 8;
    out->cellWidth = glyphs[0];
    out->cellHeight = glyphs[1];
    out->baseline = static_cast<std::int8_t>(glyphs[2]);
    const std::uint32_t sheetSize = be32(glyphs + 4);
    const int sheetCount = be16(glyphs + 8);
    const auto format = static_cast<TexFormat>(be16(glyphs + 10) & 0x7FFF);
    const int perRow = be16(glyphs + 12);
    const int rows = be16(glyphs + 14);
    const int sheetWidth = be16(glyphs + 16);
    const int sheetHeight = be16(glyphs + 18);
    const std::uint32_t sheetsAt = be32(glyphs + 20);
    if ((be16(glyphs + 10) & 0x8000) != 0) {
        return fail(error, "compressed font sheets are not supported");
    }
    if (perRow <= 0 || rows <= 0 || sheetsAt > size || static_cast<std::uint64_t>(sheetSize) * sheetCount > size - sheetsAt) {
        return fail(error, "font sheets out of range");
    }

    // Character code -> glyph index, from the maps in their chain order (the
    // first map that covers a code decides it, as nw4r::ut::ResFontBase does).
    std::vector<std::pair<std::uint32_t, std::uint16_t>> codes;
    std::vector<bool> decided(0x10000, false);
    for (std::uint32_t map = mapsAt; map != 0;) {
        if (map < 8 || map + 12 > size) {
            return fail(error, "character map out of range");
        }
        const std::uint8_t* m = f + map;
        const std::uint16_t begin = be16(m), end = be16(m + 2), method = be16(m + 4);
        const std::uint32_t next = be32(m + 8);
        const std::uint8_t* mapInfo = m + 12;
        auto add = [&](std::uint32_t code, std::uint16_t index) {
            if (!decided[code]) {
                decided[code] = true;
                if (index != 0xFFFF && (!want || want(code))) codes.emplace_back(code, index);
            }
        };
        if (method == 0) {
            if (map + 14 > size) return fail(error, "character map out of range");
            for (std::uint32_t c = begin; c <= end; ++c) add(c, static_cast<std::uint16_t>(be16(mapInfo) + (c - begin)));
        } else if (method == 1) {
            if (end < begin || map + 12 + (end - begin + 1u) * 2 > size) return fail(error, "character map out of range");
            for (std::uint32_t c = begin; c <= end; ++c) add(c, be16(mapInfo + (c - begin) * 2));
        } else if (method == 2) {
            const std::uint16_t count = be16(mapInfo);
            if (map + 14 + count * 4u > size) return fail(error, "character map out of range");
            for (std::uint16_t i = 0; i < count; ++i) add(be16(mapInfo + 2 + i * 4), be16(mapInfo + 4 + i * 4));
        } else {
            return fail(error, "unknown character map method");
        }
        if (next == map) break;
        map = next;
    }

    // Glyph index -> widths, from the CWDH chain.
    struct Widths {
        int left, glyphWidth, advance;
    };
    auto widthsOf = [&](std::uint16_t index, Widths* w) {
        for (std::uint32_t block = widthsAt; block != 0;) {
            if (block + 8 > size) return false;
            const std::uint8_t* b = f + block;
            const std::uint16_t first = be16(b), last = be16(b + 2);
            if (index >= first && index <= last) {
                const std::size_t entry = block + 8 + static_cast<std::size_t>(index - first) * 3;
                if (entry + 3 > size) return false;
                *w = {static_cast<std::int8_t>(f[entry]), f[entry + 1], static_cast<std::int8_t>(f[entry + 2])};
                return true;
            }
            const std::uint32_t next = be32(b + 4);
            if (next == block) break;
            block = next;
        }
        // FINF's default widths.
        *w = {static_cast<std::int8_t>(info[4]), info[5], static_cast<std::int8_t>(info[6])};
        return true;
    };

    std::sort(codes.begin(), codes.end());
    std::vector<std::vector<std::uint8_t>> sheets(static_cast<std::size_t>(sheetCount));
    const int perSheet = perRow * rows;
    out->glyphs.clear();
    for (const auto& [code, index] : codes) {
        const int sheet = index / perSheet;
        const int cell = index % perSheet;
        if (sheet >= sheetCount) {
            continue;
        }
        if (sheets[sheet].empty() &&
            !decodeTexture(format, sheetWidth, sheetHeight, f + sheetsAt + static_cast<std::size_t>(sheet) * sheetSize,
                           sheetSize, &sheets[sheet])) {
            return fail(error, "unsupported font sheet format " + std::to_string(static_cast<int>(format)));
        }
        Widths w{};
        if (!widthsOf(index, &w)) {
            return fail(error, "width table out of range");
        }
        Glyph glyph;
        glyph.code = code;
        glyph.left = w.left;
        glyph.advance = w.advance;
        glyph.width = std::clamp(w.glyphWidth, 0, out->cellWidth);
        glyph.height = out->cellHeight;
        const int x0 = (cell % perRow) * (out->cellWidth + 1);
        const int y0 = (cell / perRow) * (out->cellHeight + 1);
        if (x0 + glyph.width > sheetWidth || y0 + glyph.height > sheetHeight) {
            continue;
        }
        glyph.rgba.resize(static_cast<std::size_t>(glyph.width) * glyph.height * 4);
        for (int y = 0; y < glyph.height; ++y) {
            std::memcpy(glyph.rgba.data() + static_cast<std::size_t>(y) * glyph.width * 4,
                        sheets[sheet].data() + (static_cast<std::size_t>(y0 + y) * sheetWidth + x0) * 4,
                        static_cast<std::size_t>(glyph.width) * 4);
        }
        out->glyphs.push_back(std::move(glyph));
    }
    if (out->glyphs.empty()) {
        return fail(error, "font has none of the wanted characters");
    }
    return true;
}

namespace {

bool latin(std::uint32_t c) {
    return (c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c <= 0xFF);
}

bool messageCharacter(std::uint32_t c) {
    switch (c) {
    case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201C: case 0x201D:  // dashes, quotes
    case 0x2022: case 0x2026: case 0x2122:                                          // bullet, ellipsis, TM
    case 0x2190: case 0x2191: case 0x2192: case 0x2193:                              // arrows
        return true;
    default:
        return latin(c);
    }
}

}  // namespace

bool load(const std::filesystem::path& fontArchive, Set* out, std::string* error) {
    std::ifstream in(fontArchive, std::ios::binary);
    if (!in) {
        return fail(error, "cannot read " + fontArchive.string());
    }
    const std::vector<std::uint8_t> packed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::uint8_t> archive;
    if (!yaz0Decompress(packed.data(), packed.size(), &archive)) {
        archive = packed;  // an uncompressed archive (a mod may replace it)
    }
    struct Want {
        const char* file;
        Font* font;
        std::function<bool(std::uint32_t)> filter;
    };
    const Want wants[] = {
        {"messagefont26.brfnt", &out->message, messageCharacter},
        {"menufont64.brfnt", &out->menu, latin},
        {"picturefont.brfnt", &out->picture, {}},
    };
    for (const Want& want : wants) {
        std::vector<std::uint8_t> file;
        if (!rarcFile(archive, want.file, &file)) {
            return fail(error, fontArchive.string() + ": no " + want.file);
        }
        std::string why;
        if (!parseRfnt(file, want.filter, want.font, &why)) {
            return fail(error, fontArchive.string() + ": " + want.file + ": " + why);
        }
    }
    return true;
}

}  // namespace PetariNative::HomeMenu::GalaxyFont
