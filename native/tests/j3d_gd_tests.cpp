// J3DGDSetTexLookupMode encodes the texture LOD bias as the Wii does. The bias is
// converted through a signed integer on the Wii (fctiwz), so a negative bias keeps its
// two's complement byte. A direct negative float to u8 conversion is undefined, and
// optimized arm64 code leaked the sign bits through BP_TEX_MODE0 into the register id:
// the mode0 write went to BP register 0xFF, the map kept the previous wrap and filter
// (the CLAMP of J3DSys::reinitTexture), and Honeyhive's lawn (LOD bias -1) drew flat.
//
// Usage: petari_j3d_gd_tests
// Links: petari_j3d.
#include <JSystem/J3DGraphBase/J3DGD.hpp>
#include <revolution/gd/GDBase.h>

#include <cstdint>
#include <cstdio>

namespace {

int sChecks = 0;
int sFailures = 0;

void check(bool condition, const char* label) {
    ++sChecks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++sFailures;
    }
}

std::uint32_t readU32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0]) << 24 | std::uint32_t(bytes[1]) << 16 | std::uint32_t(bytes[2]) << 8 | bytes[3];
}

// The two BP commands of one call: mode0 then mode1, each 0x61 followed by the register word.
void lookupMode(float lodBias, std::uint32_t* mode0, std::uint32_t* mode1) {
    alignas(32) static std::uint8_t buffer[64];
    GDLObj list;
    GDInitGDLObj(&list, buffer, sizeof(buffer));
    GDSetCurrent(&list);
    J3DGDSetTexLookupMode(GX_TEXMAP0, GX_REPEAT, GX_REPEAT, GX_LIN_MIP_LIN, GX_LINEAR, 0.0f, 2.0f, lodBias, GX_FALSE, GX_FALSE,
                          GX_ANISO_1);
    GDSetCurrent(nullptr);
    check(buffer[0] == 0x61 && buffer[5] == 0x61, "two BP commands");
    *mode0 = readU32(buffer + 1);
    *mode1 = readU32(buffer + 6);
}

void testLodBias() {
    std::uint32_t mode0 = 0, mode1 = 0;
    // Repeat both ways, linear magnification, LIN_MIP_LIN (hardware 6), edge LOD off.
    constexpr std::uint32_t kBase = 0x80000000u | 0x1 | 0x1 << 2 | 0x1 << 4 | 0x6 << 5 | 0x1 << 8;

    lookupMode(0.0f, &mode0, &mode1);
    check(mode0 == kBase, "bias 0: mode0 register 0x80");
    check(mode1 == 0x84002000u, "bias 0: mode1 register 0x84, max LOD 2");

    // The Honeyhive lawn's value: 0x01C1D5 in the disc's material display list.
    lookupMode(-1.0f, &mode0, &mode1);
    check(mode0 >> 24 == 0x80, "bias -1: the register id stays 0x80");
    check(mode0 == (kBase | 0xE0u << 9), "bias -1: bias byte 0xE0, as on the Wii");
    check(mode1 == 0x84002000u, "bias -1: mode1 unchanged");

    lookupMode(-4.0f, &mode0, &mode1);
    check(mode0 == (kBase | 0x80u << 9), "bias -4: bias byte 0x80");

    lookupMode(1.5f, &mode0, &mode1);
    check(mode0 == (kBase | 0x30u << 9), "bias 1.5: bias byte 0x30");
}

}  // namespace

int main() {
    testLodBias();
    if (sFailures != 0) {
        std::fprintf(stderr, "native J3D GD tests: %d of %d checks failed\n", sFailures, sChecks);
        return 1;
    }
    std::printf("native J3D GD tests passed (%d checks)\n", sChecks);
    return 0;
}
