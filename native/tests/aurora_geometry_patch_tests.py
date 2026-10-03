#!/usr/bin/env python3
"""Check capacity/offset agreement and rejection before FIFO buffer mutation."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
AURORA = Path(sys.argv.pop(1))
spec = importlib.util.spec_from_file_location('geometry_patch', ROOT / 'native/gx/patch_aurora_geometry.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GeometryPatch(unittest.TestCase):
    def source(self, path):
        return (AURORA / 'lib' / path).read_text()

    def test_capacity_and_segment_offsets(self):
        frame = self.source('gfx/frame.cpp') + (ROOT / 'native/gx/efb_frame.inc').read_text()
        patched = module.patch(frame, 'frame.cpp')
        self.assertIn('PetariGeometry::stagingBytes(StagingBufferSize)', patched)
        self.assertIn('mapBuffer(frame.verts, PetariGeometry::VertexBytes)', patched)
        self.assertIn('mapBuffer(frame.indices, PetariGeometry::IndexBytes)', patched)
        self.assertIn('{PetariGeometry::VertexBytes, UniformBufferSize, PetariGeometry::IndexBytes,', patched)
        encoded = module.patch(self.source('gfx/encoding.cpp'), 'encoding.cpp')
        self.assertIn('VertexStagingOffset + PetariGeometry::VertexBytes', encoded)
        self.assertIn('IndexStagingOffset + PetariGeometry::IndexBytes', encoded)

    def test_fifo_skip_consumes_payload_before_mutation(self):
        text = module.patch(self.source('gx/command_processor.cpp'), 'command_processor.cpp')
        draw = text[text.index('static void draw_prim('):text.index('static void draw_prim(') + 4500]
        self.assertLess(draw.index('petari_aurora_geometry_fits'), draw.index('gfx::push_verts'))
        self.assertIn('reader.skip(totalVtxBytes);\n    return;', draw)
        self.assertIn('lastDraw->vtxCount) + vtxCount <= 0xffff', draw)
        indexed = text[text.index('} else if (subCmd == GX_AURORA_DRAW_INDEXED)'):]
        self.assertLess(indexed.index('reader.take(totalVtxBytes)'), indexed.index('petari_aurora_geometry_fits'))
        self.assertLess(indexed.index('petari_aurora_geometry_fits'), indexed.index('gfx::push_indices'))
        self.assertLess(indexed.index('petari_aurora_geometry_fits'), indexed.index('gfx::push_verts'))

    def test_upstream_drift_fails_closed(self):
        with self.assertRaises(ValueError):
            module.patch('changed upstream source', 'command_processor.cpp')


if __name__ == '__main__':
    unittest.main()
