#!/usr/bin/env python3
"""Which placement table each class is placed in on the disc, for the catalog's "table" field.

Reads every StageData archive of your own copy of the disc (only read) and writes
native/editor/catalog/tables.json: placement name -> the table it is placed in most,
with the layer left out ("jmp/placement/objinfo", "jmp/mapparts/mappartsinfo", ...),
and the ObjectData archives holding collision (a .kcl), for the catalog's hasCollision, with
the box their collision triangles fill (model units, scale 1) for the catalog's collisionBox.
gen_catalog.py reads it. Facts about the data only; nothing of the disc is copied.

  gen_class_tables.py [--disc build/game-data/RMGE01/files]
"""
import argparse
import collections
import struct
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "native/tools"))
from stage_edit import Archive, Table  # noqa: E402


def kcl_box(data):
    """The box a KCL's triangles fill: each prism's corners from its base point, face and edge
    normals and length (the game's KCollision triangle layout)."""
    pos_off, nrm_off, prism_off, octree_off = struct.unpack_from(">IIII", data, 0)
    def vec(off, i):
        return struct.unpack_from(">fff", data, off + 12 * i)
    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    def dot(a, b):
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
    lo, hi = [1e30] * 3, [-1e30] * 3
    count = (octree_off - (prism_off + 0x10)) // 0x10
    for i in range(count):
        height, p, f, e1, e2, e3 = struct.unpack_from(">fHHHHH", data, prism_off + 0x10 + 0x10 * i)
        a, fn = vec(pos_off, p), vec(nrm_off, f)
        ca, cb, n3 = cross(vec(nrm_off, e1), fn), cross(vec(nrm_off, e2), fn), vec(nrm_off, e3)
        da, db = dot(ca, n3), dot(cb, n3)
        if abs(da) < 1e-9 or abs(db) < 1e-9:
            continue
        b = tuple(a[k] + cb[k] * (height / db) for k in range(3))
        c = tuple(a[k] + ca[k] * (height / da) for k in range(3))
        for v in (a, b, c):
            for k in range(3):
                lo[k], hi[k] = min(lo[k], v[k]), max(hi[k], v[k])
    return None if lo[0] > hi[0] else [round(x, 1) for x in lo + hi]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--disc", type=Path, default=ROOT / "build/game-data/RMGE01/files")
    args = parser.parse_args()
    use = collections.defaultdict(collections.Counter)
    for path in sorted((args.disc / "StageData").glob("*.arc")):
        try:
            archive = Archive(path.read_bytes())
        except Exception:
            continue
        for name in archive.files:
            parts = name.lower().split("/")
            if len(parts) != 4 or parts[0] != "jmp" or parts[1] not in ("placement", "mapparts", "childobj") or parts[3] == "stageobjinfo":
                continue
            try:
                table = Table(archive.contents.get(archive.files[name]) or archive.read(name))
                table.field("name")
            except Exception:
                continue
            for row in range(len(table.rows)):
                use[table.get(row, "name")]["/".join((parts[0], parts[1], parts[3]))] += 1
    tables = {name: counts.most_common(1)[0][0] for name, counts in sorted(use.items())}
    collision = []
    boxes = {}
    for path in sorted((args.disc / "ObjectData").glob("*.arc")):
        try:
            archive = Archive(path.read_bytes())
        except Exception:
            continue
        kcls = [name for name in archive.files if name.lower().endswith(".kcl")]
        if kcls:
            collision.append(path.stem)
            try:
                # The archive's own-named collision first, else the first one.
                kcl = next((k for k in kcls if k.lower() == path.stem.lower() + ".kcl"), kcls[0])
                box = kcl_box(archive.contents.get(archive.files[kcl]) or archive.read(kcl))
                if box:
                    boxes[path.stem] = box
            except Exception:
                pass
    target = ROOT / "native/editor/catalog/tables.json"
    target.write_text(json.dumps({"source": "native/editor/tools/gen_class_tables.py", "tables": tables, "collision": collision, "collisionBoxes": boxes}, indent=0) + "\n")
    print(f"{len(tables)} classes, {len(collision)} archives with collision -> {target.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
