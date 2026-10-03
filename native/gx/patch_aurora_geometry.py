#!/usr/bin/env python3
"""Bound FIFO geometry recording and enlarge the matching GPU/staging buffers."""
from pathlib import Path
import re
import sys


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f'Aurora geometry anchor mismatch: {old[:100]}')
    return text.replace(old, new)


def patch(text, kind):
    if kind == 'frame.cpp':
        # Keep upstream inline constants unchanged (no ODR disagreement). All uses
        # allocating, mapping, and submitting geometry use our separate constants.
        text = re.sub(r'\bStagingBufferSize\b', 'PetariGeometry::stagingBytes(StagingBufferSize)', text)
        text = re.sub(r'\bVertexBufferSize\b', 'PetariGeometry::VertexBytes', text)
        text = re.sub(r'\bIndexBufferSize\b', 'PetariGeometry::IndexBytes', text)
        text += '''
static_assert(aurora::gfx::VertexBufferSize == PetariGeometry::OriginalVertexBytes);
static_assert(aurora::gfx::IndexBufferSize == PetariGeometry::OriginalIndexBytes);
'''
    elif kind == 'encoding.cpp':
        text = once(text, 'VertexStagingOffset + VertexBufferSize', 'VertexStagingOffset + PetariGeometry::VertexBytes')
        text = once(text, 'IndexStagingOffset + IndexBufferSize', 'IndexStagingOffset + PetariGeometry::IndexBytes')
    elif kind == 'recording.cpp':
        text += '''
namespace aurora::gfx {
extern "C" bool petari_aurora_geometry_fits(std::size_t vertices, std::size_t indices) {
  if (!g_recorder.active()) return false;
  const auto& frame = current_frame_packet();
  static const size_t testLimit = [] {
    const char* setting = std::getenv("PETARI_GX_GEOMETRY_TEST_BYTES");
    if (!setting) return PetariGeometry::IndexBytes;
    const auto value = std::strtoull(setting, nullptr, 10);
    return value >= 4096 ? std::min<size_t>(value, PetariGeometry::IndexBytes) : PetariGeometry::IndexBytes;
  }();
  if (PetariGeometry::fits(frame.verts.size(), vertices, std::min(PetariGeometry::VertexBytes, testLimit)) &&
      PetariGeometry::fits(frame.indices.size(), indices, testLimit)) return true;
  static uint64_t warnedFrame = UINT64_MAX;
  if (warnedFrame != frame.frameId) {
    warnedFrame = frame.frameId;
    std::fprintf(stderr, "[gx-geometry] frame %llu: skipping draw beyond geometry capacity "
                 "(vertices %zu + %zu / %zu, indices %zu + %zu / %zu)\\n",
                 static_cast<unsigned long long>(frame.frameId), frame.verts.size(), vertices,
                 std::min(PetariGeometry::VertexBytes, testLimit), frame.indices.size(), indices, testLimit);
  }
  return false;
}
} // namespace aurora::gfx
'''
        text = '#include <cstdio>\n#include <cstdlib>\n#include <algorithm>\n' + text
    elif kind == 'command_processor.cpp':
        text = once(text, 'const bool canMerge = lastDraw != nullptr && lastDraw->instanceCount == 1;', '''const bool canMerge = lastDraw != nullptr && lastDraw->instanceCount == 1 &&
                        static_cast<uint32_t>(lastDraw->vtxCount) + vtxCount <= 0xffff;
  // Worst-case triangle expansion plus an index conversion of the preceding draw.
  // Consume rejected FIFO payloads completely so subsequent commands remain aligned.
  const size_t indexBound = static_cast<size_t>(vtxCount) * 6 +
                           (canMerge ? static_cast<size_t>(lastDraw->vtxCount) * 6 : 0) + 16;
  if (!petari_aurora_geometry_fits(totalVtxBytes, indexBound)) {
    reader.skip(totalVtxBytes);
    return;
  }''')
        text = once(text, '    const gfx::Range idxRange = gfx::push_indices(indexData.data(), indexData.size(), 4);\n', '')
        text = once(text, '''    const auto vertexData = reader.take(totalVtxBytes);
    const gfx::Range vertRange = gfx::push_verts(vertexData.data(), vertexData.size(), 4);''', '''    const auto vertexData = reader.take(totalVtxBytes);
    if (!petari_aurora_geometry_fits(vertexData.size(), indexData.size())) return;
    const gfx::Range idxRange = gfx::push_indices(indexData.data(), indexData.size(), 4);
    const gfx::Range vertRange = gfx::push_verts(vertexData.data(), vertexData.size(), 4);''')
    else:
        raise ValueError(f'Unsupported geometry source: {kind}')
    return '#include "geometry_budget.hpp"\n' + text


if __name__ == '__main__':
    source, output = map(Path, sys.argv[1:])
    text = patch(source.read_text(), source.name)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)
