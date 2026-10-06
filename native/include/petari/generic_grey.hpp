#pragma once
// The Generic world mod (src/Game/System/GenericWorld.cpp): colours and GX textures without hue,
// brightness and alpha kept. Used where particles, layouts and text are loaded or drawn.
//
// The hooks below are weak defaults that change nothing, for programs without the game (tests);
// GenericWorld.cpp defines the real ones (it includes this header with PETARI_GENERIC_GREY_IMPL).
#include <algorithm>
#include <cstdint>

#ifndef PETARI_GENERIC_GREY_IMPL
// True while particle and layout colours should lose their hue (the mod on).
extern "C" __attribute__((weak)) bool petari_generic_world_grey_particles() {
    return false;
}
// An RGBA8 colour (0xRRGGBBAA) as drawn: greyed while the mod is on.
extern "C" __attribute__((weak)) std::uint32_t petari_generic_world_grey_rgba(std::uint32_t rgba) {
    return rgba;
}
#endif

namespace PetariNative::Grey {

inline int luma(int r, int g, int b) {
    return (r * 299 + g * 587 + b * 114) / 1000;
}
inline std::uint32_t rgba(std::uint32_t v) {
    const int g = luma(static_cast< int >((v >> 24) & 255), static_cast< int >((v >> 16) & 255), static_cast< int >((v >> 8) & 255));
    return (static_cast< std::uint32_t >(g) << 24) | (static_cast< std::uint32_t >(g) << 16) | (static_cast< std::uint32_t >(g) << 8) | (v & 255);
}
inline std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast< std::uint16_t >((p[0] << 8) | p[1]);
}
inline void wr16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast< std::uint8_t >(v >> 8);
    p[1] = static_cast< std::uint8_t >(v);
}
inline std::uint16_t c565(std::uint16_t v) {
    const int g = luma(((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31);
    return static_cast< std::uint16_t >(((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
}
inline std::uint16_t c5A3(std::uint16_t v) {
    if ((v & 0x8000) != 0) {
        const int g = luma(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31) >> 3;
        return static_cast< std::uint16_t >(0x8000 | (g << 10) | (g << 5) | g);
    }
    const int g = luma(((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17) / 17;
    return static_cast< std::uint16_t >((v & 0x7000) | (g << 8) | (g << 4) | g);
}

// One CMPR block, keeping its mode (c0 > c1: four colours; else three and transparent): when
// greying flips the endpoint order, the endpoints swap and the indices follow.
inline void cmprBlock(std::uint8_t* b) {
    const std::uint16_t c0 = rd16(b), c1 = rd16(b + 2);
    const bool opaque = c0 > c1;
    std::uint16_t g0 = c565(c0), g1 = c565(c1);
    if (opaque ? g0 < g1 : g0 > g1) {
        std::swap(g0, g1);
        for (int row = 0; row < 4; ++row) {
            std::uint8_t out = 0;
            for (int t = 0; t < 4; ++t) {
                int i = (b[4 + row] >> (6 - 2 * t)) & 3;
                i = opaque ? (i ^ 1) : (i == 0 ? 1 : i == 1 ? 0 : i);
                out = static_cast< std::uint8_t >(out | (i << (6 - 2 * t)));
            }
            b[4 + row] = out;
        }
    }
    if (opaque && g0 == g1) {
        g0 = static_cast< std::uint16_t >(g1 | 1);
        g1 = static_cast< std::uint16_t >(g1 & 0xFFFE);
    }
    wr16(b, g0);
    wr16(b + 2, g1);
}

// A big-endian GX image (all levels) in place; intensity formats are grey already. Returns its size.
inline std::uint32_t texture(int fmt, int w, int h, int levels, std::uint8_t* data, const std::uint8_t* end) {
    int bw, bh, bytes;
    switch (fmt) {
    case 4: case 5: bw = 4; bh = 4; bytes = 32; break;  // RGB565, RGB5A3
    case 6: bw = 4; bh = 4; bytes = 64; break;          // RGBA8
    case 14: bw = 8; bh = 8; bytes = 32; break;         // CMPR
    default: return 0;
    }
    std::uint8_t* p = data;
    for (int level = 0; level < std::max(1, levels); ++level) {
        const int tiles = ((w + bw - 1) / bw) * ((h + bh - 1) / bh);
        for (int t = 0; t < tiles && p + bytes <= end; ++t, p += bytes) {
            if (fmt == 14) {
                for (int k = 0; k < 4; ++k) cmprBlock(p + k * 8);
            } else if (fmt == 6) {
                for (int k = 0; k < 16; ++k) {
                    const std::uint8_t g = static_cast< std::uint8_t >(luma(p[k * 2 + 1], p[32 + k * 2], p[32 + k * 2 + 1]));
                    p[k * 2 + 1] = p[32 + k * 2] = p[32 + k * 2 + 1] = g;
                }
            } else {
                for (int k = 0; k < 16; ++k) wr16(p + k * 2, fmt == 4 ? c565(rd16(p + k * 2)) : c5A3(rd16(p + k * 2)));
            }
        }
        w = std::max(1, w >> 1);
        h = std::max(1, h >> 1);
    }
    return static_cast< std::uint32_t >(p - data);
}

// A big-endian palette in place (1 RGB565, 2 RGB5A3; IA8 is grey already).
inline void palette(int palFmt, int count, std::uint8_t* pal, const std::uint8_t* end) {
    for (int i = 0; i < count && pal + i * 2 + 2 <= end; ++i) {
        if (palFmt == 1) wr16(pal + i * 2, c565(rd16(pal + i * 2)));
        if (palFmt == 2) wr16(pal + i * 2, c5A3(rd16(pal + i * 2)));
    }
}

}  // namespace PetariNative::Grey
