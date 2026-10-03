#pragma once
#include <cstddef>

namespace PetariGeometry {
    inline constexpr std::size_t VertexBytes = 8 * 1024 * 1024;
    inline constexpr std::size_t IndexBytes = 8 * 1024 * 1024;
    inline constexpr std::size_t OriginalVertexBytes = 5 * 1024 * 1024;
    inline constexpr std::size_t OriginalIndexBytes = 2 * 1024 * 1024;
    constexpr std::size_t stagingBytes(std::size_t original) {
        return original + VertexBytes - OriginalVertexBytes + IndexBytes - OriginalIndexBytes;
    }
    constexpr bool fits(std::size_t used, std::size_t requested, std::size_t capacity, std::size_t alignment = 4) {
        if (used > capacity)
            return false;
        const std::size_t padding = alignment ? (alignment - used % alignment) % alignment : 0;
        return padding <= capacity - used && requested <= capacity - used - padding;
    }
    constexpr bool fitsGeometry(std::size_t verticesUsed, std::size_t indicesUsed, std::size_t vertices, std::size_t indices) {
        return fits(verticesUsed, vertices, VertexBytes) && fits(indicesUsed, indices, IndexBytes);
    }
}  // namespace PetariGeometry
// FIFO worker only: preflight both streams before publishing any part of a draw.
extern "C" bool petari_aurora_geometry_fits(std::size_t vertices, std::size_t indices);
