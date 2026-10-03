#!/usr/bin/env python3
"""Which placement table each class is placed in on the disc, for the catalog's "table" field.

Reads every StageData archive of your own copy of the disc (only read) and writes
native/editor/catalog/tables.json: placement name -> the table it is placed in most,
with the layer left out ("jmp/placement/objinfo", "jmp/mapparts/mappartsinfo", ...),
and the ObjectData archives holding collision (a .kcl), for the catalog's hasCollision.
gen_catalog.py reads it. Facts about the data only; nothing of the disc is copied.

  gen_class_tables.py [--disc build/game-data/RMGE01/files]
"""
import argparse
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "native/tools"))
from stage_edit import Archive, Table  # noqa: E402


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
    for path in sorted((args.disc / "ObjectData").glob("*.arc")):
        try:
            archive = Archive(path.read_bytes())
        except Exception:
            continue
        if any(name.lower().endswith(".kcl") for name in archive.files):
            collision.append(path.stem)
    target = ROOT / "native/editor/catalog/tables.json"
    target.write_text(json.dumps({"source": "native/editor/tools/gen_class_tables.py", "tables": tables, "collision": collision}, indent=0) + "\n")
    print(f"{len(tables)} classes, {len(collision)} archives with collision -> {target.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
