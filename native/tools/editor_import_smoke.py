#!/usr/bin/env python3
"""Generate a CC0 textured glTF, import through the sheet, and check live undo/save in a private fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib
from test_server import main_checkout


def fixture(folder):
    # Original generated cube and checker pixels, dedicated to CC0.
    vertices = []
    uvs = []
    indices = []
    for face in [((-1, 0, -1), (1, 0, -1), (1, 2, -1), (-1, 2, -1)),
                 ((1, 0, 1), (-1, 0, 1), (-1, 2, 1), (1, 2, 1)),
                 ((-1, 0, 1), (-1, 0, -1), (-1, 2, -1), (-1, 2, 1)),
                 ((1, 0, -1), (1, 0, 1), (1, 2, 1), (1, 2, -1)),
                 ((-1, 2, -1), (1, 2, -1), (1, 2, 1), (-1, 2, 1)),
                 ((-1, 0, 1), (1, 0, 1), (1, 0, -1), (-1, 0, -1))]:
        base = len(vertices)
        vertices.extend(face)
        uvs.extend(((0, 0), (1, 0), (1, 1), (0, 1)))
        indices.extend((base, base+2, base+1, base, base+3, base+2))
    pos = b''.join(struct.pack('<3f', *p) for p in vertices)
    uv = b''.join(struct.pack('<2f', *p) for p in uvs)
    idx = struct.pack('<' + 'H'*len(indices), *indices)
    (folder/'cube.bin').write_bytes(pos+uv+idx)
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))
    pixels = b''.join(b'\0'+b''.join(bytes((255, 180, 50, 255) if (x//4+y//4)%2 else (30, 125, 235, 255)) for x in range(32)) for y in range(32))
    png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 32, 8, 6, 0, 0, 0))+chunk(b'IDAT', zlib.compress(pixels))+chunk(b'IEND', b'')
    (folder/'checker.png').write_bytes(png)
    gltf = dict(asset={'version': '2.0', 'copyright': 'CC0 generated fixture'},
                buffers=[dict(uri='cube.bin', byteLength=len(pos+uv+idx))],
                bufferViews=[dict(buffer=0, byteOffset=0, byteLength=len(pos)), dict(buffer=0, byteOffset=len(pos), byteLength=len(uv)), dict(buffer=0, byteOffset=len(pos+uv), byteLength=len(idx))],
                accessors=[dict(bufferView=0, componentType=5126, count=24, type='VEC3'), dict(bufferView=1, componentType=5126, count=24, type='VEC2'), dict(bufferView=2, componentType=5123, count=36, type='SCALAR')],
                images=[dict(uri='checker.png')], textures=[dict(source=0)],
                materials=[dict(name='CC0 checker', doubleSided=True, pbrMetallicRoughness=dict(baseColorTexture=dict(index=0)))],
                meshes=[dict(primitives=[dict(attributes={'POSITION': 0, 'TEXCOORD_0': 1}, indices=2, material=0)])],
                nodes=[dict(mesh=0)], scenes=[dict(nodes=[0])], scene=0)
    path=folder/'CC0 Checker Cube.gltf'
    path.write_text(json.dumps(gltf))
    return path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a=p.parse_args()
    root=Path(__file__).resolve().parents[2]
    main=main_checkout()
    out=a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    source=fixture(out)
    user=out/'user'; app=a.app.resolve()
    subprocess.run([sys.executable,str(root/'native/tools/create_observatory_fixture.py'),'--source',str(main/'build/observatory-user-2'),'--output',str(user),'--kind','stage'],check=True)
    (out/'revision.txt').write_text(subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True)+hashlib.sha256(app.read_bytes()).hexdigest()+'\n')
    env=dict(os.environ,PETARI_SMOKE='stage',PETARI_STAGE='EggStarGalaxy',PETARI_SCENARIO='1',PETARI_SMOKE_FRAMES='14000',PETARI_STAGE_IDLE_FRAMES='1800',PETARI_STAGE_GATE_MS='0',PETARI_FAST_BOOT='1',PETARI_PIPELINE_GLOBAL_PRECOMPILE='0',PETARI_LOCK_CLASS='functional',PETARI_MODS='LevelEditor',PETARI_EDITOR_IMPORT_TEST=str(out),PETARI_EDITOR_IMPORT_SHEET=str(source),PETARI_SURFACE_DUMP=str(out/'surfaces'),PETARI_SURFACE_DUMP_LABELS='import-sheet,import-placed',PETARI_SURFACE_DUMP_EVERY='5',PETARI_SURFACE_DUMP_SPAN='15')
    cmd=[str(main/'build/locked-app.sh'),'editor-paths','/usr/bin/perl','-e','alarm shift; exec @ARGV or die','600',str(app),'--disc',str(main/'build/game-data/RMGE01'),'--user',str(user),'--test-fixture','stage']
    try:
        with (out/'run.log').open('w') as log: result=subprocess.run(cmd,env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT)
        text=(out/'run.log').read_text(errors='replace')
        for line in text.splitlines():
            if '[editor-import]' in line or 'PETARI SMOKE RESULT:' in line: print(line)
        return int(result.returncode!=0 or '[editor-import] PASS' not in text or '[editor-import] FAIL' in text)
    finally: shutil.rmtree(user/'cache',ignore_errors=True)


if __name__=='__main__':
    raise SystemExit(main())
