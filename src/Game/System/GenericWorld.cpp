// Native mod: Generic world (include/Game/System/GenericWorld.hpp).
//
// Textures: every texture of a loaded model is rewritten in place in its own GX
// format, without decoding, so sizes, mip levels, wrap modes and alpha
// cut-outs stay exactly as they were. The colours become one of three muted
// tones (picked by the original's brightness) in a 16-texel checker, or a
// starfield for skies. The original bytes are kept on the host, so a model
// loaded with the mod off gets them back.
//
// Stand-ins: a DrawBuffer holds every visible instance of one model; for the
// models below it draws a plain shape at each instance's root joint instead.
#ifdef PETARI_NATIVE
#include "Game/System/GenericWorld.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/System/DrawBuffer.hpp"
#include "Game/Util/GenericShapes.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/J3DGraphBase/J3DVertex.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <petari/host_allocation.hpp>
#include <petari/mods.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    // ---- Roles -------------------------------------------------------------

    enum class Role : u8 { None, Enemy, Coin, PurpleCoin, Gem, Star, Spring, LaunchStar, ArrowSwitch };

    struct Named {
        const char* archive;
        Role role;
    };

    // By archive (root folder) name, case-insensitive.
    constexpr Named kStandIns[] = {
        {"Kuribo", Role::Enemy},          {"KuriboMini", Role::Enemy},          {"Takobo", Role::Enemy},
        {"NokonokoLand", Role::Enemy},    {"Coin", Role::Coin},                 {"PurpleCoin", Role::PurpleCoin},
        {"StarPiece", Role::Gem},         {"PowerStar", Role::Star},            {"GrandStar", Role::Star},
        {"JumpStand", Role::Spring},      {"SuperSpinDriver", Role::LaunchStar}, {"SpinDriver", Role::LaunchStar},
        {"ArrowSwitch", Role::ArrowSwitch},
    };

    struct ModelInfo {
        Role role = Role::None;
        TVec3f min{0.0f, 0.0f, 0.0f}, max{0.0f, 0.0f, 0.0f};
        bool bounds = false;
    };

    std::mutex sMutex;
    std::unordered_map< const J3DModelData*, ModelInfo > sModels;

    bool tracing() {
        static const bool sOn = std::getenv("PETARI_GENERIC_WORLD_TRACE") != nullptr;
        return sOn;
    }

    std::string lower(const char* p) {
        std::string s(p != nullptr ? p : "");
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast< char >(std::tolower(c)); });
        return s;
    }

    // The archive name without folders or ".arc".
    std::string baseName(const char* pArchive) {
        std::string s = lower(pArchive);
        const size_t slash = s.find_last_of('/');
        if (slash != std::string::npos) {
            s = s.substr(slash + 1);
        }
        if (s.size() > 4 && s.compare(s.size() - 4, 4, ".arc") == 0) {
            s.resize(s.size() - 4);
        }
        return s;
    }

    // The file name without folders or extension.
    std::string fileBase(const char* pFile) {
        std::string s = baseName(pFile);
        const size_t dot = s.find_last_of('.');
        return dot == std::string::npos ? s : s.substr(0, dot);
    }

    // The player's models stay as they are (the GenericCharacter mod owns them).
    bool isPlayer(const std::string& archive) {
        return archive.find("mario") != std::string::npos || archive.find("luigi") != std::string::npos;
    }

    bool isSky(const std::string& archive) {
        const size_t at = archive.find("sky");
        // "...Sky", "...SkyA" and the VR backdrops (VROrbit, VRDarkSpace, ...).
        return (at != std::string::npos && archive.size() - at <= 4) || archive.compare(0, 2, "vr") == 0;
    }

    // ---- Texture rewrite ---------------------------------------------------

    struct Rgb {
        u8 r, g, b;
    };

    // Three muted tones (light and dark of each for the checker): mid greys, so
    // the game's bright planet lights do not wash them out.
    constexpr Rgb kTones[3][2] = {
        {{0x70, 0x7C, 0x88}, {0x60, 0x6C, 0x78}},  // slate (dark originals)
        {{0x94, 0x96, 0x90}, {0x80, 0x82, 0x7C}},  // stone
        {{0xAE, 0xA4, 0x90}, {0x9A, 0x90, 0x7C}},  // sand (bright originals)
    };
    constexpr Rgb kSkyBase = {0x0C, 0x10, 0x22};
    constexpr Rgb kSkyStar = {0xE8, 0xEC, 0xFF};

    int luma(int r, int g, int b) {
        return (r * 299 + g * 587 + b * 114) / 1000;
    }
    u16 rd16(const u8* p) {
        return static_cast< u16 >((p[0] << 8) | p[1]);
    }
    void wr16(u8* p, u16 v) {
        p[0] = static_cast< u8 >(v >> 8);
        p[1] = static_cast< u8 >(v);
    }
    u16 to565(const Rgb& c) {
        return static_cast< u16 >(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
    }
    f32 lum(u8 r, u8 g, u8 b) {
        return (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
    }
    f32 lum565(u16 v) {
        return lum(static_cast< u8 >(((v >> 11) & 31) << 3), static_cast< u8 >(((v >> 5) & 63) << 2), static_cast< u8 >((v & 31) << 3));
    }
    f32 lum5A3(u16 v) {
        if ((v & 0x8000) != 0) {
            return lum(static_cast< u8 >(((v >> 10) & 31) << 3), static_cast< u8 >(((v >> 5) & 31) << 3), static_cast< u8 >((v & 31) << 3));
        }
        return lum(static_cast< u8 >(((v >> 8) & 15) << 4), static_cast< u8 >(((v >> 4) & 15) << 4), static_cast< u8 >((v & 15) << 4));
    }
    u16 to5A3(const Rgb& c, u16 original) {
        if ((original & 0x8000) != 0) {
            return static_cast< u16 >(0x8000 | ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3));
        }
        return static_cast< u16 >((original & 0x7000) | ((c.r >> 4) << 8) | ((c.g >> 4) << 4) | (c.b >> 4));
    }
    u8 grey(const Rgb& c) {
        return static_cast< u8 >((c.r * 299 + c.g * 587 + c.b * 114) / 1000);
    }

    struct Format {
        int bw, bh, tileBytes;
    };

    bool formatOf(int fmt, Format* pOut) {
        switch (fmt) {
        case GX_TF_I4: *pOut = {8, 8, 32}; return true;
        case GX_TF_I8: *pOut = {8, 4, 32}; return true;
        case GX_TF_IA4: *pOut = {8, 4, 32}; return true;
        case GX_TF_IA8: *pOut = {4, 4, 32}; return true;
        case GX_TF_RGB565: *pOut = {4, 4, 32}; return true;
        case GX_TF_RGB5A3: *pOut = {4, 4, 32}; return true;
        case GX_TF_RGBA8: *pOut = {4, 4, 64}; return true;
        case GX_TF_C4: *pOut = {8, 8, 32}; return true;
        case GX_TF_C8: *pOut = {8, 4, 32}; return true;
        case GX_TF_C14X2: *pOut = {4, 4, 32}; return true;
        case GX_TF_CMPR: *pOut = {8, 8, 32}; return true;
        default: return false;
        }
    }

    bool paletted(int fmt) {
        return fmt == GX_TF_C4 || fmt == GX_TF_C8 || fmt == GX_TF_C14X2;
    }

    // Visits every tile of every level: (tile bytes, the tile's texel origin in level-0 texels, the level's texel scale).
    template < typename F >
    u32 eachTile(const ResTIMG& rImg, u8* pData, F&& visit) {
        Format f;
        if (!formatOf(rImg.mFormat, &f)) {
            return 0;
        }
        u32 offset = 0;
        int w = rImg.mWidth, h = rImg.mHeight;
        const int levels = std::max< int >(1, rImg.mImageNum);
        for (int level = 0; level < levels; ++level) {
            const int tilesX = (w + f.bw - 1) / f.bw, tilesY = (h + f.bh - 1) / f.bh;
            for (int ty = 0; ty < tilesY; ++ty) {
                for (int tx = 0; tx < tilesX; ++tx) {
                    if (pData != nullptr) {
                        visit(pData + offset, (tx * f.bw) << level, (ty * f.bh) << level, 1 << level);
                    }
                    offset += f.tileBytes;
                }
            }
            w = std::max(1, w >> 1);
            h = std::max(1, h >> 1);
        }
        return offset;
    }

    struct Look {
        bool sky;
        int tone;
        // The colour at a level-0 texel.
        Rgb at(int x, int y) const {
            if (sky) {
                return kSkyBase;
            }
            return kTones[tone][((x >> 4) ^ (y >> 4)) & 1];
        }
    };

    // A star in this 4x4 block (a hash of its position): one block in 14.
    bool starBlock(int x, int y) {
        u32 h = static_cast< u32 >(x) * 73856093u ^ static_cast< u32 >(y) * 19349663u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        return (h % 14u) == 0;
    }

    // The original's mean brightness (0..1), from what each format stores without decoding.
    f32 meanBrightness(const ResTIMG& rImg, u8* pData, const u8* pPalette) {
        f32 sum = 0.0f;
        u32 n = 0;
        const int fmt = rImg.mFormat;
        if (paletted(fmt)) {
            for (int i = 0; i < rImg.mPaletteNum && pPalette != nullptr; ++i) {
                const u16 v = rd16(pPalette + i * 2);
                sum += rImg.mPaletteFormat == GX_TL_RGB565 ? lum565(v) : rImg.mPaletteFormat == GX_TL_RGB5A3 ? lum5A3(v) : (v & 0xFF) / 255.0f;
                ++n;
            }
            return n != 0 ? sum / n : 0.5f;
        }
        Format f;
        formatOf(fmt, &f);
        u32 tiles = 0;
        eachTile(rImg, pData, [&](u8* p, int, int, int scale) {
            if (scale != 1 || (tiles++ & 3) != 0) {
                return;  // level 0, every fourth tile
            }
            switch (fmt) {
            case GX_TF_CMPR:
                for (int b = 0; b < 4; ++b) {
                    sum += lum565(rd16(p + b * 8)) + lum565(rd16(p + b * 8 + 2));
                    n += 2;
                }
                break;
            case GX_TF_RGB565:
                for (int i = 0; i < 16; ++i, ++n) {
                    sum += lum565(rd16(p + i * 2));
                }
                break;
            case GX_TF_RGB5A3:
                for (int i = 0; i < 16; ++i, ++n) {
                    sum += lum5A3(rd16(p + i * 2));
                }
                break;
            case GX_TF_RGBA8:
                for (int i = 0; i < 16; ++i, ++n) {
                    sum += lum(p[i * 2 + 1], p[32 + i * 2], p[32 + i * 2 + 1]);
                }
                break;
            case GX_TF_I4:
                for (int i = 0; i < 32; ++i, n += 2) {
                    sum += ((p[i] >> 4) + (p[i] & 15)) / 15.0f;
                }
                break;
            case GX_TF_I8:
                for (int i = 0; i < 32; ++i, ++n) {
                    sum += p[i] / 255.0f;
                }
                break;
            case GX_TF_IA4:
                for (int i = 0; i < 32; ++i, ++n) {
                    sum += (p[i] & 15) / 15.0f;
                }
                break;
            case GX_TF_IA8:
                for (int i = 0; i < 16; ++i, ++n) {
                    sum += p[i * 2 + 1] / 255.0f;
                }
                break;
            default:
                break;
            }
        });
        return n != 0 ? sum / n : 0.5f;
    }

    void rewriteTile(int fmt, const Look& rLook, u8* p, int x0, int y0, int scale) {
        switch (fmt) {
        case GX_TF_CMPR:
            for (int b = 0; b < 4; ++b) {
                u8* block = p + b * 8;
                const int bx = x0 + (b & 1) * 4 * scale, by = y0 + (b >> 1) * 4 * scale;
                const u16 c0 = rd16(block), c1 = rd16(block + 2);
                const bool opaqueMode = c0 > c1;
                if (rLook.sky && opaqueMode) {
                    // White and navy: every texel navy, one in a few blocks a star.
                    wr16(block, to565(kSkyStar));
                    wr16(block + 2, to565(kSkyBase));
                    for (int row = 0; row < 4; ++row) {
                        block[4 + row] = 0x55;
                    }
                    if (scale == 1 && starBlock(bx, by)) {
                        const int k = (bx * 7 + by * 3) & 15;
                        block[4 + k / 4] = static_cast< u8 >(block[4 + k / 4] & ~(3 << (6 - 2 * (k % 4))));
                    }
                    continue;
                }
                const u16 c = to565(rLook.at(bx, by));
                if (opaqueMode) {
                    // Keep c0 > c1 (the opaque four-colour mode): two near-equal endpoints.
                    const u16 low = static_cast< u16 >(c & 0xFFFE);
                    wr16(block, static_cast< u16 >(low | 1));
                    wr16(block + 2, low);
                } else {
                    // Keep c0 <= c1 (three colours and transparent): index 3 stays transparent.
                    wr16(block, c);
                    wr16(block + 2, c);
                }
            }
            break;
        case GX_TF_RGB565:
            for (int i = 0; i < 16; ++i) {
                wr16(p + i * 2, to565(rLook.at(x0 + (i % 4) * scale, y0 + (i / 4) * scale)));
            }
            break;
        case GX_TF_RGB5A3:
            for (int i = 0; i < 16; ++i) {
                wr16(p + i * 2, to5A3(rLook.at(x0 + (i % 4) * scale, y0 + (i / 4) * scale), rd16(p + i * 2)));
            }
            break;
        case GX_TF_RGBA8:
            for (int i = 0; i < 16; ++i) {
                const Rgb c = rLook.at(x0 + (i % 4) * scale, y0 + (i / 4) * scale);
                p[i * 2 + 1] = c.r;  // AR pairs: alpha kept
                p[32 + i * 2] = c.g;
                p[32 + i * 2 + 1] = c.b;
            }
            break;
        case GX_TF_I4:
            for (int i = 0; i < 32; ++i) {
                const u8 g = static_cast< u8 >(grey(rLook.at(x0 + (i * 2 % 8) * scale, y0 + (i * 2 / 8) * scale)) >> 4);
                const u8 hi = p[i] >> 4, lo = p[i] & 15;
                p[i] = static_cast< u8 >(((hi < 2 ? hi : g) << 4) | (lo < 2 ? lo : g));  // near-black kept (often a mask)
            }
            break;
        case GX_TF_I8:
            for (int i = 0; i < 32; ++i) {
                if (p[i] >= 24) {
                    p[i] = grey(rLook.at(x0 + (i % 8) * scale, y0 + (i / 8) * scale));
                }
            }
            break;
        case GX_TF_IA4:
            for (int i = 0; i < 32; ++i) {
                p[i] = static_cast< u8 >((p[i] & 0xF0) | (grey(rLook.at(x0 + (i % 8) * scale, y0 + (i / 8) * scale)) >> 4));
            }
            break;
        case GX_TF_IA8:
            for (int i = 0; i < 16; ++i) {
                p[i * 2 + 1] = grey(rLook.at(x0 + (i % 4) * scale, y0 + (i / 4) * scale));  // alpha byte kept
            }
            break;
        default:
            break;
        }
    }

    void rewritePalette(const ResTIMG& rImg, u8* pPalette, const Look& rLook) {
        const Rgb c = rLook.at(0, 0);
        for (int i = 0; i < rImg.mPaletteNum; ++i) {
            u8* e = pPalette + i * 2;
            const u16 v = rd16(e);
            switch (rImg.mPaletteFormat) {
            case GX_TL_RGB565: wr16(e, to565(c)); break;
            case GX_TL_RGB5A3: wr16(e, to5A3(c, v)); break;
            default: e[1] = grey(c); break;  // IA8: alpha (high byte) kept
            }
        }
    }

    // Backups: the original bytes of each rewritten region, and a hash of what we
    // wrote, so a region is recognised as ours (and not reused memory) later.
    struct Backup {
        std::vector< u8 > original;
        u64 written;
    };
    std::unordered_map< const u8*, Backup > sBackups;

    u64 hashOf(const u8* p, u32 n) {
        u64 h = 1469598103934665603ull;
        for (u32 i = 0; i < n; ++i) {
            h = (h ^ p[i]) * 1099511628211ull;
        }
        return h;
    }

    bool ours(const u8* p, u32 n) {
        const auto it = sBackups.find(p);
        return it != sBackups.end() && it->second.original.size() == n && it->second.written == hashOf(p, n);
    }

    // Mod off: our rewrite (if this is still it) goes back to the original.
    void restore(u8* p, u32 n) {
        if (ours(p, n)) {
            std::memcpy(p, sBackups[p].original.data(), n);
            sBackups.erase(p);
        }
    }

    void neutralise(ResTIMG* pImg, bool sky, int* pCount) {
        u8* data = reinterpret_cast< u8* >(pImg) + JUT_RESTIMG_OFFSET(pImg->mImageDataOffset);
        u8* palette = paletted(pImg->mFormat) ? reinterpret_cast< u8* >(pImg) + JUT_RESTIMG_OFFSET(pImg->mPaletteDataOffset) : nullptr;
        const u32 size = eachTile(*pImg, nullptr, [](u8*, int, int, int) {});
        const u32 paletteSize = palette != nullptr ? pImg->mPaletteNum * 2u : 0u;
        if (size == 0) {
            return;  // a format we do not know
        }
        u8* region = palette != nullptr ? palette : data;
        const u32 regionSize = palette != nullptr ? paletteSize : size;
        if (ours(region, regionSize)) {
            return;  // shared with a texture already rewritten
        }
        Backup backup;
        backup.original.assign(region, region + regionSize);
        const f32 bright = meanBrightness(*pImg, data, palette);
        Look look{sky, bright < 0.30f ? 0 : bright < 0.55f ? 1 : 2};
        if (palette != nullptr) {
            rewritePalette(*pImg, palette, look);
        } else {
            const int fmt = pImg->mFormat;
            eachTile(*pImg, data, [&](u8* p, int x, int y, int scale) { rewriteTile(fmt, look, p, x, y, scale); });
        }
        backup.written = hashOf(region, regionSize);
        sBackups[region] = std::move(backup);
        ++*pCount;
    }

    void restoreTexture(ResTIMG* pImg) {
        u8* data = reinterpret_cast< u8* >(pImg) + JUT_RESTIMG_OFFSET(pImg->mImageDataOffset);
        if (paletted(pImg->mFormat)) {
            restore(reinterpret_cast< u8* >(pImg) + JUT_RESTIMG_OFFSET(pImg->mPaletteDataOffset), pImg->mPaletteNum * 2u);
        } else {
            restore(data, eachTile(*pImg, nullptr, [](u8*, int, int, int) {}));
        }
    }

    // ---- Vertex colours -----------------------------------------------------

    u32 hostOrBig(u32 v) {
        return v > 0xFFFF ? __builtin_bswap32(v) : v;  // the list as the loader left it, either order
    }

    // The component type of a colour attribute (GX_VA_CLR0/1), or -1.
    int colorType(const J3DVertexData& rVertex, int attr) {
        const GXVtxAttrFmtList* list = rVertex.mVtxAttrFmtList;
        for (int i = 0; list != nullptr && i < 32; ++i) {
            const u32 a = hostOrBig(static_cast< u32 >(list[i].attr));
            if (a == GX_VA_NULL) {
                break;
            }
            if (a == static_cast< u32 >(attr)) {
                return static_cast< int >(hostOrBig(static_cast< u32 >(list[i].type)));
            }
        }
        return -1;
    }

    // Baked vertex colours (shading and tint) lose their hue and keep their brightness.
    void greyVertexColours(J3DModelData* pData, bool rewrite) {
        J3DVertexData& v = pData->getVertexData();
        for (int set = 0; set < 2; ++set) {
            u8* colours = reinterpret_cast< u8* >(v.mVtxColorArray[set]);
            const u32 bytes = v.mNativeArrayBytes[3 + set];
            const int type = colorType(v, GX_VA_CLR0 + set);
            const int stride = type == GX_RGBA8 || type == GX_RGBX8 ? 4 : type == GX_RGB8 || type == GX_RGBA6 ? 3
                                                                        : type == GX_RGB565 || type == GX_RGBA4 ? 2 : 0;
            if (colours == nullptr || bytes == 0 || stride == 0) {
                continue;
            }
            if (!rewrite) {
                restore(colours, bytes);
                continue;
            }
            if (ours(colours, bytes)) {
                continue;
            }
            Backup backup;
            backup.original.assign(colours, colours + bytes);
            for (u32 i = 0; i + stride <= bytes; i += stride) {
                u8* c = colours + i;
                if (type == GX_RGB565) {  // big-endian 5:6:5
                    const u16 v = rd16(c);
                    const int g = luma(((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31);
                    wr16(c, static_cast< u16 >(((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3)));
                } else if (type == GX_RGBA4) {  // 4:4:4:4
                    const u16 v = rd16(c);
                    const int g = luma(((v >> 12) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17) / 17;
                    wr16(c, static_cast< u16 >((g << 12) | (g << 8) | (g << 4) | (v & 15)));
                } else if (type == GX_RGBA6) {  // 6:6:6:6 in 24 bits
                    const u32 v = (static_cast< u32 >(c[0]) << 16) | (static_cast< u32 >(c[1]) << 8) | c[2];
                    const int g = luma(((v >> 18) & 63) * 4, ((v >> 12) & 63) * 4, ((v >> 6) & 63) * 4) / 4;
                    const u32 out = (static_cast< u32 >(g) << 18) | (static_cast< u32 >(g) << 12) | (static_cast< u32 >(g) << 6) | (v & 63);
                    c[0] = static_cast< u8 >(out >> 16);
                    c[1] = static_cast< u8 >(out >> 8);
                    c[2] = static_cast< u8 >(out);
                } else {
                    c[0] = c[1] = c[2] = static_cast< u8 >(luma(c[0], c[1], c[2]));
                }
            }
            backup.written = hashOf(colours, bytes);
            sBackups[colours] = std::move(backup);
        }
    }

    // ---- Material colours (in the model file, before J3D loads it) ----------

    u32 rd32(const u8* p) {
        return (static_cast< u32 >(p[0]) << 24) | (static_cast< u32 >(p[1]) << 16) | (static_cast< u32 >(p[2]) << 8) | p[3];
    }
    void wr32(u8* p, u32 v) {
        p[0] = static_cast< u8 >(v >> 24);
        p[1] = static_cast< u8 >(v >> 16);
        p[2] = static_cast< u8 >(v >> 8);
        p[3] = static_cast< u8 >(v);
    }

    // Every byte range we change is backed up first (restored with the mod off).
    void backupOnce(u8* p, u32 n) {
        if (n != 0 && sBackups.find(p) == sBackups.end()) {
            Backup b;
            b.original.assign(p, p + n);
            b.written = 0;
            sBackups[p] = std::move(b);
        }
    }
    void sealBackup(u8* p, u32 n) {
        const auto it = sBackups.find(p);
        if (it != sBackups.end()) {
            it->second.written = hashOf(p, n);
        }
    }

    void greyRgba8(u8* p) {
        const u8 g = static_cast< u8 >(luma(p[0], p[1], p[2]));
        p[0] = p[1] = p[2] = g;
    }

    // A material display list: BP TEV colour registers (0xE0..0xE7: RA then BG
    // per register) and XF material/ambient colours (0x100A..0x100D).
    void greyDisplayList(u8* p, u32 size) {
        int ra[4] = {0, 0, 0, 0};
        u8* raAt[4] = {nullptr, nullptr, nullptr, nullptr};
        u32 i = 0;
        while (i < size) {
            const u8 op = p[i];
            if (op == 0x00 || op == 0x48) {
                i += 1;
            } else if (op == 0x08) {
                i += 6;
            } else if (op == 0x61 && i + 5 <= size) {
                u8* reg = p + i + 1;
                const u32 v = rd32(reg);
                const u32 id = v >> 24;
                if (id >= 0xE0 && id <= 0xE7) {
                    const int index = (id - 0xE0) / 2;
                    const bool konst = (v & 0x800000) != 0;
                    auto field = [&](int shift) {
                        const int raw = static_cast< int >((v >> shift) & 0x7FF);
                        return konst ? (raw & 0xFF) : (raw >= 0x400 ? raw - 0x800 : raw);
                    };
                    auto pack = [&](int value) { return static_cast< u32 >(konst ? std::clamp(value, 0, 255) : (value & 0x7FF)); };
                    if (((id - 0xE0) & 1) == 0) {
                        ra[index] = field(0);  // red; alpha (bits 12..22) stays
                        raAt[index] = reg;
                    } else if (raAt[index] != nullptr) {
                        const int g = luma(ra[index], field(12), field(0));
                        const u32 raV = rd32(raAt[index]);
                        wr32(raAt[index], (raV & ~0x7FFu) | pack(g));
                        wr32(reg, (v & ~0x7FF7FFu) | (pack(g) << 12) | pack(g));
                    }
                }
                i += 5;
            } else if (op == 0x10 && i + 5 <= size) {
                const u32 count = static_cast< u32 >(rd16(p + i + 1)) + 1;
                const u32 addr = rd16(p + i + 3);
                for (u32 k = 0; k < count && i + 5 + k * 4 + 4 <= size; ++k) {
                    if (addr + k >= 0x100A && addr + k <= 0x100D) {
                        greyRgba8(p + i + 5 + k * 4);
                    }
                }
                i += 5 + count * 4;
            } else if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {
                i += 5;
            } else if (op == 0x40) {
                i += 9;
            } else {
                break;  // a draw or something unexpected: stop here
            }
        }
    }

    // The model file's sections, by tag.
    u8* section(u8* pFile, u32 fileSize, const char* tag, u32* pSize) {
        if (fileSize < 0x20) {
            return nullptr;
        }
        const u32 count = rd32(pFile + 0x0C);
        u32 offset = 0x20;
        for (u32 n = 0; n < count && offset + 8 <= fileSize; ++n) {
            const u32 size = rd32(pFile + offset + 4);
            if (std::memcmp(pFile + offset, tag, 4) == 0) {
                *pSize = size;
                return pFile + offset;
            }
            if (size == 0) {
                break;
            }
            offset += size;
        }
        return nullptr;
    }

    void greyModelFile(u8* pFile, bool rewrite) {
        if (std::memcmp(pFile, "J3D2", 4) != 0) {
            return;
        }
        const u32 fileSize = rd32(pFile + 8);
        u32 matSize = 0, mdlSize = 0;
        u8* mat = section(pFile, fileSize, "MAT3", &matSize);
        u8* mdl = section(pFile, fileSize, "MDL3", &mdlSize);
        for (u8* sec : {mat, mdl}) {
            const u32 n = sec == mat ? matSize : mdlSize;
            if (sec == nullptr) {
                continue;
            }
            if (!rewrite) {
                restore(sec, n);
                continue;
            }
            if (ours(sec, n)) {
                return;  // done already
            }
        }
        if (!rewrite) {
            return;
        }
        if (mat != nullptr) {
            backupOnce(mat, matSize);
            u32 offsets[30];
            for (int k = 0; k < 30; ++k) {
                offsets[k] = rd32(mat + 0x0C + k * 4);
            }
            auto extent = [&](int k) {
                u32 end = matSize;
                for (int j = 0; j < 30; ++j) {
                    if (offsets[j] > offsets[k] && offsets[j] < end) {
                        end = offsets[j];
                    }
                }
                return offsets[k] != 0 ? end - offsets[k] : 0u;
            };
            for (int k : {5, 8, 18}) {  // material, ambient and konst colours: RGBA8
                for (u32 o = 0; o + 4 <= extent(k); o += 4) {
                    greyRgba8(mat + offsets[k] + o);
                }
            }
            for (u32 o = 0; o + 8 <= extent(17); o += 8) {  // TEV colours: RGBA s16
                u8* c = mat + offsets[17] + o;
                const int g = luma(static_cast< s16 >(rd16(c)), static_cast< s16 >(rd16(c + 2)), static_cast< s16 >(rd16(c + 4)));
                wr16(c, static_cast< u16 >(g));
                wr16(c + 2, static_cast< u16 >(g));
                wr16(c + 4, static_cast< u16 >(g));
            }
            sealBackup(mat, matSize);
        }
        if (mdl != nullptr) {
            backupOnce(mdl, mdlSize);
            const u32 count = rd16(mdl + 8);
            const u32 packets = rd32(mdl + 0x0C);
            for (u32 k = 0; k < count && packets + k * 8 + 8 <= mdlSize; ++k) {
                u8* entry = mdl + packets + k * 8;
                const u32 dl = rd32(entry), size = rd32(entry + 4);
                if (packets + k * 8 + dl + size <= mdlSize) {
                    greyDisplayList(entry + dl, size);
                }
            }
            sealBackup(mdl, mdlSize);
        }
    }

    // ---- Bounds ------------------------------------------------------------

    bool vertexBounds(J3DModelData* pData, TVec3f* pMin, TVec3f* pMax) {
        const J3DVertexData& v = pData->getVertexData();
        const u32 n = v.getVtxNum();
        if (n == 0 || v.getVtxPosArray() == nullptr) {
            return false;
        }
        TVec3f lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
        for (u32 i = 0; i < n; ++i) {
            TVec3f p;
            if (v.getVtxPosType() == GX_F32) {
                const f32* f = static_cast< const f32* >(v.getVtxPosArray()) + i * 3;
                p.set(f[0], f[1], f[2]);
            } else if (v.getVtxPosType() == GX_S16) {
                const s16* s = static_cast< const s16* >(v.getVtxPosArray()) + i * 3;
                const f32 k = 1.0f / static_cast< f32 >(1 << v.getVtxPosFrac());
                p.set(s[0] * k, s[1] * k, s[2] * k);
            } else {
                return false;
            }
            lo.set(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
            hi.set(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
        }
        *pMin = lo;
        *pMax = hi;
        return hi.x > lo.x && hi.y > lo.y;
    }

    // ---- Stand-ins ---------------------------------------------------------

    constexpr GXColor kEnemy = {0xE0, 0x5A, 0x2A, 0xFF};
    constexpr GXColor kEnemyDark = {0x6A, 0x24, 0x14, 0xFF};
    constexpr GXColor kEyeWhite = {0xF4, 0xF0, 0xE8, 0xFF};
    constexpr GXColor kEyeDark = {0x20, 0x1C, 0x1C, 0xFF};
    constexpr GXColor kGold = {0xEC, 0xC2, 0x30, 0xFF};
    constexpr GXColor kViolet = {0x9A, 0x5C, 0xD8, 0xFF};
    constexpr GXColor kGem = {0xF4, 0xDC, 0x78, 0xFF};
    constexpr GXColor kCyan = {0x30, 0xC8, 0xD8, 0xFF};
    constexpr GXColor kCyanDark = {0x1C, 0x7C, 0x88, 0xFF};
    constexpr GXColor kSwitch = {0x3C, 0x7C, 0xF0, 0xFF};
    constexpr GXColor kSwitchBase = {0x50, 0x58, 0x64, 0xFF};

    // A joint's world matrix by name (the root's when the model has no such joint).
    MtxPtr jointMtx(const J3DModel* pModel, const char* pName) {
        const s32 i = pModel->mModelData->mJointTree.mJointName->getIndex(pName);
        return pModel->mMtxBuffer->mpAnmMtx[i >= 0 ? i : 0];
    }

    // m * translate(c) * rotateX(90 degrees): a y-axis shape turned to face z.
    void facingZ(const Mtx m, const TVec3f& c, Mtx out) {
        Mtx t, r;
        PSMTXTrans(t, c.x, c.y, c.z);
        PSMTXRotRad(r, 'x', 1.5707963f);
        PSMTXConcat(m, t, out);
        PSMTXConcat(out, r, out);
    }

    void drawStandIn(Role role, const ModelInfo& rInfo, const J3DModel* pModel) {
        const MtxPtr m = pModel->mMtxBuffer->mpAnmMtx[0];
        const TVec3f zero(0.0f, 0.0f, 0.0f);
        TVec3f lo(-60.0f, 0.0f, -60.0f), hi(60.0f, 120.0f, 60.0f);  // with no bounds
        if (rInfo.bounds) {
            lo = rInfo.min;
            hi = rInfo.max;
        }
        const TVec3f size(hi - lo), mid((lo + hi) * 0.5f);
        const f32 wide = std::max(size.x, std::max(size.y, size.z));
        switch (role) {
        case Role::Enemy: {
            // A plain round body with two eyes and two feet (front is +z).
            const TVec3f body(mid.x, lo.y + size.y * 0.45f, mid.z);
            GenericShapes::color(kEnemy);
            GenericShapes::ellipsoid(m, body, TVec3f(size.x * 0.42f, size.y * 0.40f, size.z * 0.42f), false);
            GenericShapes::color(kEnemyDark);
            for (const f32 side : {-1.0f, 1.0f}) {
                GenericShapes::ellipsoid(m, TVec3f(mid.x + side * size.x * 0.2f, lo.y + size.y * 0.06f, mid.z + size.z * 0.08f),
                                         TVec3f(size.x * 0.14f, size.y * 0.07f, size.z * 0.2f), false);
            }
            for (const f32 side : {-1.0f, 1.0f}) {
                const TVec3f eye(mid.x + side * size.x * 0.14f, lo.y + size.y * 0.58f, mid.z + size.z * 0.36f);
                GenericShapes::color(kEyeWhite);
                GenericShapes::ellipsoid(m, eye, TVec3f(size.x * 0.08f, size.y * 0.1f, size.z * 0.06f), false);
                GenericShapes::color(kEyeDark);
                GenericShapes::ellipsoid(m, eye + TVec3f(0.0f, 0.0f, size.z * 0.05f), TVec3f(size.x * 0.04f, size.y * 0.05f, size.z * 0.03f),
                                         false);
            }
            break;
        }
        case Role::Coin:
        case Role::PurpleCoin: {
            Mtx disc;
            facingZ(m, mid, disc);
            const f32 r = std::max(size.x, size.y) * 0.45f;
            GenericShapes::color(role == Role::Coin ? kGold : kViolet);
            GenericShapes::cylinder(disc, zero, r, r * 0.12f);
            break;
        }
        case Role::Gem:
            GenericShapes::color(kGem);
            GenericShapes::ellipsoid(m, mid, TVec3f(wide * 0.3f, wide * 0.38f, wide * 0.3f), false);
            break;
        case Role::Star: {
            const f32 r = rInfo.bounds ? std::max(size.x, size.y) * 0.5f : 80.0f;
            GenericShapes::color(kGold);
            GenericShapes::starPrism(m, rInfo.bounds ? mid : TVec3f(0.0f, 0.0f, 0.0f), r, r * 0.45f, r * 0.16f);
            break;
        }
        case Role::Spring: {
            // A squat cylinder (the pad) on a narrower one (the spring).
            const f32 r = std::max(size.x, size.z) * 0.45f;
            GenericShapes::color(kCyanDark);
            GenericShapes::cylinder(m, TVec3f(mid.x, lo.y + size.y * 0.3f, mid.z), r * 0.6f, size.y * 0.3f);
            GenericShapes::color(kCyan);
            GenericShapes::cylinder(m, TVec3f(mid.x, lo.y + size.y * 0.75f, mid.z), r, size.y * 0.15f);
            break;
        }
        case Role::LaunchStar: {
            const f32 r = rInfo.bounds ? std::max(size.x, size.y) * 0.5f : 120.0f;
            GenericShapes::color(kCyan);
            GenericShapes::starPrism(m, rInfo.bounds ? mid : TVec3f(0.0f, 0.0f, 0.0f), r, r * 0.5f, r * 0.12f);
            break;
        }
        case Role::ArrowSwitch: {
            // ArrowSwitch.bdl: a block on "Base" and a flat arrow on "Head" (pointing along the
            // joint's +y, 60 thick), which turns with the switch. Sizes from its joint bounds.
            GenericShapes::color(kSwitchBase);
            GenericShapes::box(jointMtx(pModel, "Base"), TVec3f(-75.0f, -74.0f, -42.0f), TVec3f(75.0f, 91.0f, 42.0f));
            GenericShapes::color(kSwitch);
            GenericShapes::arrowPrism(jointMtx(pModel, "Head"), 91.0f, 230.0f, 335.0f, 36.0f, 96.0f, 30.0f);
            break;
        }
        case Role::None:
            break;
        }
    }
}  // namespace

namespace GenericWorld {
    bool on() {
        return PetariNative::Mods::enabled(PetariNative::Mods::Mod::GenericWorld);
    }

    void onModelData(J3DModelData* pModelData, const char* pArchive, const char* pFile) {
        if (pModelData == nullptr) {
            return;
        }
        // The maps, strings and backups are host memory: on a game thread they
        // would otherwise come from the current JKR heap, which a scene change frees.
        PetariNative::HostAllocationScope host;
        const std::string archive = baseName(pArchive);
        const std::string file = fileBase(pFile);
        ModelInfo info;
        for (const Named& named : kStandIns) {
            if (archive == lower(named.archive) && file == archive) {
                info.role = named.role;
            }
        }
        info.bounds = vertexBounds(pModelData, &info.min, &info.max);
        std::lock_guard< std::mutex > lock(sMutex);
        sModels[pModelData] = info;  // a new model at a freed one's address replaces it
        if (isPlayer(archive)) {
            return;
        }
        greyVertexColours(pModelData, on());
        J3DTexture* textures = pModelData->getTexture();
        if (textures == nullptr) {
            return;
        }
        const bool sky = isSky(archive);
        int count = 0;
        for (u16 i = 0; i < textures->getNum(); ++i) {
            ResTIMG* img = textures->getResTIMG(i);
            if (img == nullptr) {
                continue;
            }
            if (on()) {
                neutralise(img, sky, &count);
            } else {
                restoreTexture(img);
            }
        }
        if (tracing()) {
            std::fprintf(stderr, "[generic-world] %s/%s: %d of %d textures rewritten%s%s\n", archive.c_str(), file.c_str(), count,
                         textures->getNum(), sky ? " (sky)" : "", info.role != Role::None ? ", stand-in" : "");
        }
    }

    void onModelFile(void* pFile, const char* pArchive) {
        if (pFile == nullptr || isPlayer(baseName(pArchive))) {
            return;
        }
        PetariNative::HostAllocationScope host;
        std::lock_guard< std::mutex > lock(sMutex);
        greyModelFile(static_cast< u8* >(pFile), on());
    }

    bool drawBuffer(const DrawBuffer* pBuffer, bool opaque) {
        if (!on() || pBuffer == nullptr || pBuffer->mNumActors <= 0 || pBuffer->mNumShapeDrawers <= 0) {
            return false;
        }
        ModelInfo info;
        {
            PetariNative::HostAllocationScope host;
            std::lock_guard< std::mutex > lock(sMutex);
            const auto it = sModels.find(pBuffer->mModelData);
            if (it == sModels.end() || it->second.role == Role::None) {
                return false;
            }
            info = it->second;
        }
        if (!opaque) {
            return true;  // the stand-ins are opaque: nothing in the translucent pass
        }
        if (tracing()) {
            static int sLogged[16] = {};
            if (sLogged[static_cast< int >(info.role)]++ % 600 == 0) {
                std::fprintf(stderr, "[generic-world] stand-in role %d: %d instance packets\n", static_cast< int >(info.role),
                             static_cast< int >(pBuffer->getShapeDrawerByIndex(0)->mNumPackets));
            }
        }
        const DrawBufferShapeDrawer* drawer = pBuffer->getShapeDrawerByIndex(0);
        GenericShapes::begin(true);  // the player's lights unless the instance has its own
        const J3DModel* drawn[64];
        int drawnCount = 0;
        for (s32 i = 0; i < drawer->mNumPackets; ++i) {
            const PacketInfo* packet = drawer->mPackets[i];
            const J3DModel* model = packet->mShapePacket != nullptr ? packet->mShapePacket->getModel() : nullptr;
            if (model == nullptr || std::find(drawn, drawn + drawnCount, model) != drawn + drawnCount) {
                continue;  // a model with several materials in this drawer: once
            }
            if (drawnCount < 64) {
                drawn[drawnCount++] = model;
            }
            if (packet->mLightCtrl != nullptr) {
                packet->mLightCtrl->loadLight();
            }
            drawStandIn(info.role, info, model);
        }
        GenericShapes::end();
        return true;
    }
}  // namespace GenericWorld

#endif
