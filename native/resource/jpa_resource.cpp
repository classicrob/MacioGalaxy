// Conversion of big-endian JPA particle containers into host-layout images. See
// native/include/petari/jpa_resource.hpp. Layouts follow the JPA*Data structs in
// JSystem/JParticle and the readers in JPAResourceLoader.cpp / JPABaseShape.cpp.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <petari/jpa_resource.hpp>
#include <petari/endian.hpp>
#include <petari/host_allocation.hpp>
#include <cstring>

// The Generic world mod (Game/System/GenericWorld.cpp): particle colours lose their hue. This
// default, for programs without the game, keeps them.
extern "C" __attribute__((weak)) bool petari_generic_world_grey_particles() {
    return false;
}

namespace PetariNative {
namespace JPA {
    namespace {
        void greyColor(std::uint8_t* rgba);  // below, with the Generic world helpers
        const std::uint32_t cVersion = 0x322D3130;  // '2-10'

        std::uint32_t hostU32(const std::uint8_t* p) {
            std::uint32_t value;
            std::memcpy(&value, p, sizeof(value));
            return value;
        }

        class Converter {
        public:
            Converter(std::uint8_t* image, std::uint32_t size) : mImage(image), mSize(size) {
            }

            const char* run();

        private:
            bool fits(std::uint32_t offset, std::uint32_t length) const {
                return offset <= mSize && length <= mSize - offset;
            }

            void swap16(std::uint32_t offset, std::uint32_t count = 1) {
                for (std::uint32_t i = 0; i < count; i++) {
                    const std::uint16_t value = readU16BE(mImage + offset + i * 2);
                    std::memcpy(mImage + offset + i * 2, &value, sizeof(value));
                }
            }

            void swap32(std::uint32_t offset, std::uint32_t count = 1) {
                for (std::uint32_t i = 0; i < count; i++) {
                    const std::uint32_t value = readU32BE(mImage + offset + i * 4);
                    std::memcpy(mImage + offset + i * 4, &value, sizeof(value));
                }
            }

            const char* convertBlock(std::uint32_t block, std::uint32_t size, std::uint32_t tdbCount);
            const char* convertBaseShape(std::uint32_t block, std::uint32_t size);
            const char* convertTexture(std::uint32_t block, std::uint32_t size);

            std::uint8_t* mImage;
            std::uint32_t mSize;
        };

        std::uint32_t tag(const char* name) {
            return readU32BE(name);
        }

        const char* Converter::run() {
            if (mSize < 0x10 || readU32BE(mImage) != tag("JPAC") || readU32BE(mImage + 4) != cVersion) {
                return "not a big-endian JPAC2-10 container";
            }
            const std::uint32_t resourceNum = readU16BE(mImage + 0x08);
            const std::uint32_t textureNum = readU16BE(mImage + 0x0A);
            const std::uint32_t textureOffset = readU32BE(mImage + 0x0C);
            swap32(0x00, 2);
            swap16(0x08, 2);
            swap32(0x0C);

            std::uint32_t offset = 0x10;
            for (std::uint32_t i = 0; i < resourceNum; i++) {
                if (!fits(offset, 8)) {
                    return "resource header outside the container";
                }
                // u16 user index, u16 block count, u8 field/key/TDB1 counts, pad.
                const std::uint32_t blockNum = readU16BE(mImage + offset + 2);
                const std::uint32_t tdbCount = mImage[offset + 6];
                swap16(offset, 2);
                offset += 8;
                for (std::uint32_t j = 0; j < blockNum; j++) {
                    if (!fits(offset, 8)) {
                        return "block header outside the container";
                    }
                    const std::uint32_t size = readU32BE(mImage + offset + 4);
                    if (size < 8 || !fits(offset, size)) {
                        return "block size outside the container";
                    }
                    if (const char* error = convertBlock(offset, size, tdbCount)) {
                        return error;
                    }
                    swap32(offset, 2);  // tag and size
                    offset += size;
                }
            }

            if (textureNum != 0 && !fits(textureOffset, 0)) {
                return "texture table outside the container";
            }
            offset = textureOffset;
            for (std::uint32_t i = 0; i < textureNum; i++) {
                if (!fits(offset, 0x40) || readU32BE(mImage + offset) != tag("TEX1")) {
                    return "texture block missing";
                }
                const std::uint32_t size = readU32BE(mImage + offset + 4);
                if (size < 0x40 || !fits(offset, size)) {
                    return "texture block size outside the container";
                }
                if (const char* error = convertTexture(offset, size)) {
                    return error;
                }
                offset += size;
            }
            return nullptr;
        }

        const char* Converter::convertBlock(std::uint32_t block, std::uint32_t size, std::uint32_t tdbCount) {
            const std::uint32_t type = readU32BE(mImage + block);
            if (type == tag("BSP1")) {
                return convertBaseShape(block, size);
            }
            if (type == tag("BEM1")) {  // JPADynamicsBlockData (0x7C)
                if (size < 0x7C) {
                    return "BEM1 smaller than its data";
                }
                swap32(block + 0x08, 2);
                swap32(block + 0x10, (0x68 - 0x10) / 4);
                swap16(block + 0x68, (0x78 - 0x68) / 2);  // rotation, frames, life, volume size, div number
                return nullptr;
            }
            if (type == tag("FLD1")) {  // JPAFieldBlockData
                if (size < 0x44) {
                    return "FLD1 smaller than its data";
                }
                swap32(block + 0x08, (0x40 - 0x08) / 4);
                return nullptr;
            }
            if (type == tag("KFA1")) {  // key count at 0x09, f32 (time, value, in, out) keys at 0x0C
                const std::uint32_t keys = mImage[block + 0x09];
                if (0x0C + keys * 16 > size) {
                    return "KFA1 keys outside the block";
                }
                swap32(block + 0x0C, keys * 4);
                return nullptr;
            }
            if (type == tag("ESP1")) {  // JPAExtraShapeData (0x60)
                if (size < 0x60) {
                    return "ESP1 smaller than its data";
                }
                swap32(block + 0x08, (0x28 - 0x08) / 4);
                swap16(block + 0x28, 2);
                swap32(block + 0x2C, (0x60 - 0x2C) / 4);
                return nullptr;
            }
            if (type == tag("SSP1")) {  // JPAChildShapeData (0x48)
                if (size < 0x48) {
                    return "SSP1 smaller than its data";
                }
                swap32(block + 0x08, (0x34 - 0x08) / 4);
                swap32(block + 0x3C);
                swap16(block + 0x40, 2);
                swap16(block + 0x46);
                if (petari_generic_world_grey_particles()) {
                    greyColor(mImage + block + 0x34);  // mPrmClr
                    greyColor(mImage + block + 0x38);  // mEnvClr
                }
                return nullptr;
            }
            if (type == tag("ETX1")) {  // JPAExTexShapeData (0x28)
                if (size < 0x28) {
                    return "ETX1 smaller than its data";
                }
                swap32(block + 0x08, 7);
                return nullptr;
            }
            if (type == tag("TDB1")) {  // u16 texture indices
                if (8 + tdbCount * 2 > size) {
                    return "TDB1 entries outside the block";
                }
                swap16(block + 0x08, tdbCount);
                return nullptr;
            }
            return "unsupported JPA block type";
        }

        void greyColor(std::uint8_t* rgba) {
            const std::uint8_t g = static_cast< std::uint8_t >((rgba[0] * 299 + rgba[1] * 587 + rgba[2] * 114) / 1000);
            rgba[0] = rgba[1] = rgba[2] = g;
        }

        int luma8(int r, int g, int b) {
            return (r * 299 + g * 587 + b * 114) / 1000;
        }
        std::uint16_t grey565(std::uint16_t v) {
            const int g = luma8(((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31);
            return static_cast< std::uint16_t >(((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
        }
        std::uint16_t grey5A3(std::uint16_t v) {
            if ((v & 0x8000) != 0) {
                const int g = luma8(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31) >> 3;
                return static_cast< std::uint16_t >(0x8000 | (g << 10) | (g << 5) | g);
            }
            const int g = luma8(((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17) / 17;
            return static_cast< std::uint16_t >((v & 0x7000) | (g << 8) | (g << 4) | g);
        }
        std::uint16_t rd16(const std::uint8_t* p) {
            return static_cast< std::uint16_t >((p[0] << 8) | p[1]);
        }
        void wr16(std::uint8_t* p, std::uint16_t v) {
            p[0] = static_cast< std::uint8_t >(v >> 8);
            p[1] = static_cast< std::uint8_t >(v);
        }

        // One CMPR block greyed, keeping its mode (c0 > c1: four colours; else three and transparent):
        // when greying flips the endpoint order, the endpoints swap and the indices follow.
        void greyCmprBlock(std::uint8_t* b) {
            const std::uint16_t c0 = rd16(b), c1 = rd16(b + 2);
            const bool opaque = c0 > c1;
            std::uint16_t g0 = grey565(c0), g1 = grey565(c1);
            bool swap = opaque ? g0 < g1 : g0 > g1;
            if (swap) {
                std::swap(g0, g1);
                for (int row = 0; row < 4; ++row) {
                    std::uint8_t out = 0;
                    for (int t = 0; t < 4; ++t) {
                        int i = (b[4 + row] >> (6 - 2 * t)) & 3;
                        i = opaque ? (i ^ 1) : (i == 0 ? 1 : i == 1 ? 0 : i);  // 0<->1, 2<->3 (opaque); 0<->1 (alpha mode)
                        out = static_cast< std::uint8_t >(out | (i << (6 - 2 * t)));
                    }
                    b[4 + row] = out;
                }
            }
            if (opaque && g0 == g1) {
                g0 = static_cast< std::uint16_t >(g1 | 1);  // keep c0 > c1
                g1 = static_cast< std::uint16_t >(g1 & 0xFFFE);
            }
            wr16(b, g0);
            wr16(b + 2, g1);
        }

        // A particle texture's colours lose their hue in place (big-endian GX data), alpha and brightness kept.
        void greyTexture(std::uint8_t* timg, std::uint8_t* end) {
            const int fmt = timg[0];
            int w = rd16(timg + 2), h = rd16(timg + 4);
            const int levels = std::max< int >(1, timg[0x18]);
            std::uint8_t* data = timg + readU32BE(timg + 0x1C);
            if (fmt == 8 || fmt == 9 || fmt == 10) {  // C4, C8, C14X2: the palette
                std::uint8_t* pal = timg + readU32BE(timg + 0x0C);
                const int count = rd16(timg + 0x0A), palFmt = timg[0x09];
                for (int i = 0; i < count && pal + i * 2 + 2 <= end; ++i) {
                    if (palFmt == 1) wr16(pal + i * 2, grey565(rd16(pal + i * 2)));
                    if (palFmt == 2) wr16(pal + i * 2, grey5A3(rd16(pal + i * 2)));
                }
                return;
            }
            int bw, bh, bytes;
            switch (fmt) {
            case 4: case 5: bw = 4; bh = 4; bytes = 32; break;  // RGB565, RGB5A3
            case 6: bw = 4; bh = 4; bytes = 64; break;          // RGBA8
            case 14: bw = 8; bh = 8; bytes = 32; break;         // CMPR
            default: return;                                    // intensity formats are grey already
            }
            for (int level = 0; level < levels; ++level) {
                const int tiles = ((w + bw - 1) / bw) * ((h + bh - 1) / bh);
                for (int t = 0; t < tiles && data + bytes <= end; ++t, data += bytes) {
                    if (fmt == 14) {
                        for (int k = 0; k < 4; ++k) greyCmprBlock(data + k * 8);
                    } else if (fmt == 6) {
                        for (int k = 0; k < 16; ++k) {
                            const std::uint8_t g = static_cast< std::uint8_t >(luma8(data[k * 2 + 1], data[32 + k * 2], data[32 + k * 2 + 1]));
                            data[k * 2 + 1] = data[32 + k * 2] = data[32 + k * 2 + 1] = g;
                        }
                    } else {
                        for (int k = 0; k < 16; ++k) wr16(data + k * 2, fmt == 4 ? grey565(rd16(data + k * 2)) : grey5A3(rd16(data + k * 2)));
                    }
                }
                w = std::max(1, w >> 1);
                h = std::max(1, h >> 1);
            }
        }

        // JPABaseShapeData (0x34), then optional tables: texture coordinate matrix animation
        // (10 f32, flag 0x01000000), texture index animation (u8), and color key tables
        // (s16 frame + GXColor) at the prm/env offsets.
        const char* Converter::convertBaseShape(std::uint32_t block, std::uint32_t size) {
            if (size < 0x34) {
                return "BSP1 smaller than its data";
            }
            const std::uint32_t flags = readU32BE(mImage + block + 0x08);
            const std::int16_t prmOffset = static_cast< std::int16_t >(readU16BE(mImage + block + 0x0C));
            const std::int16_t envOffset = static_cast< std::int16_t >(readU16BE(mImage + block + 0x0E));
            const std::uint8_t clrFlg = mImage[block + 0x21];
            const std::uint32_t prmKeys = mImage[block + 0x22];
            const std::uint32_t envKeys = mImage[block + 0x23];

            swap32(block + 0x08);
            swap16(block + 0x0C, 2);
            swap32(block + 0x10, 2);
            swap16(block + 0x18);
            swap16(block + 0x24);

            if ((flags & 0x01000000) != 0) {
                if (0x34 + 0x28 > size) {
                    return "BSP1 texture matrix table outside the block";
                }
                swap32(block + 0x34, 10);
            }

            const struct {
                bool mEnabled;
                std::int16_t mOffset;
                std::uint32_t mKeys;
            } colorTables[2] = {{(clrFlg & 0x02) != 0, prmOffset, prmKeys}, {(clrFlg & 0x08) != 0, envOffset, envKeys}};
            for (const auto& table : colorTables) {
                if (!table.mEnabled) {
                    continue;
                }
                if (table.mOffset < 0x34 || static_cast< std::uint32_t >(table.mOffset) + table.mKeys * 6 > size) {
                    return "BSP1 color key table outside the block";
                }
                for (std::uint32_t k = 0; k < table.mKeys; k++) {
                    swap16(block + table.mOffset + k * 6);
                    if (petari_generic_world_grey_particles()) {
                        greyColor(mImage + block + table.mOffset + k * 6 + 2);  // the key's GXColor
                    }
                }
            }
            if (std::getenv("PETARI_GENERIC_WORLD_TRACE") != nullptr) {
                static int sShapes = 0;
                if (sShapes++ % 200 == 0) {
                    std::fprintf(stderr, "[generic-world] particle shape %d: grey %d\n", sShapes, petari_generic_world_grey_particles() ? 1 : 0);
                }
            }
            if (petari_generic_world_grey_particles()) {
                greyColor(mImage + block + 0x26);  // mClrPrm
                greyColor(mImage + block + 0x2A);  // mClrEnv
            }
            return nullptr;
        }

        // TEX1: tag, size, pad, 0x14-byte name, then a BTI (ResTIMG header + image data).
        const char* Converter::convertTexture(std::uint32_t block, std::uint32_t size) {
            const std::uint32_t timg = block + 0x20;
            const std::uint32_t imageOffset = readU32BE(mImage + timg + 0x1C);
            const std::uint32_t paletteOffset = readU32BE(mImage + timg + 0x0C);
            if (imageOffset > size - 0x20 || paletteOffset > size - 0x20) {
                return "texture data outside its block";
            }
            if (petari_generic_world_grey_particles()) {
                greyTexture(mImage + timg, mImage + block + size);  // before the header goes host-order
            }
            swap32(block, 3);
            swap16(timg + 0x02, 2);
            swap16(timg + 0x0A);
            swap32(timg + 0x0C);
            swap16(timg + 0x1A);
            swap32(timg + 0x1C);
            return nullptr;
        }
    }  // namespace

    ImageKind classifyImage(const void* data, std::uint32_t size) {
        const std::uint8_t* bytes = static_cast< const std::uint8_t* >(data);
        if (size < 8) {
            return ImageKind::NotJPA;
        }
        if (hostU32(bytes + 4) == cVersion) {
            return ImageKind::Host;
        }
        if (readU32BE(bytes + 4) == cVersion) {
            return ImageKind::BigEndian;
        }
        return ImageKind::NotJPA;
    }

    const char* makeHostImage(const void* src, std::uint32_t size, void* dst) {
        // The converter's tables and temporaries are host data: keep them off the game's
        // current JKR heap, which callers may destroy while this code's statics live on.
        HostAllocationScope hostAllocations;
        std::memcpy(dst, src, size);
        Converter converter(static_cast< std::uint8_t* >(dst), size);
        return converter.run();
    }
}  // namespace JPA
}  // namespace PetariNative
