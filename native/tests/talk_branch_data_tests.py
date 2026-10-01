#!/usr/bin/env python3
"""US message flows use the talk-branch numbering TalkMessageCtrl::rootNodePre remaps.

The decompiled rootNodePre follows the Korean build (15 cases, with an LED-pattern
check); the US disc (RMGE01) has 14, numbering 11 the Astro Galaxy branch (its
argument indexes TalkDirector's 28 branch flags), 12 the time-keep demo and 13
"message already read". This reads the disc's message.bmg (skipped without a
disc) and checks the flows still match that, plus the Fountain Luma's branch.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "native/tools"))

checks = 0


def check(ok, what):
    global checks
    checks += 1
    if not ok:
        print(f"FAIL: {what}")
        sys.exit(1)


def main():
    arc = ROOT / "build/game-data/RMGE01/files/UsEnglish/MessageData/Message.arc"
    if not arc.exists():
        print("SKIP: no US disc data at", arc)
        return 0
    import stage_edit
    archive = stage_edit.Archive(arc.read_bytes())
    files = {path.rsplit("/", 1)[-1]: archive.contents[index] for path, index in archive.files.items()}
    bmg = files["message.bmg"]
    sections, pos = {}, 0x20
    while pos < len(bmg) - 8:
        tag, size = bmg[pos:pos + 4], struct.unpack(">I", bmg[pos + 4:pos + 8])[0]
        if size == 0:
            break
        sections[tag] = pos
        pos += size
    flw = sections[b"FLW1"]
    node_count, branch_count = struct.unpack(">HH", bmg[flw + 8:flw + 12])
    nodes = [struct.unpack(">BBHHH", bmg[flw + 16 + 8 * i:flw + 24 + 8 * i]) for i in range(node_count)]
    targets = [struct.unpack(">H", bmg[flw + 16 + 8 * node_count + 2 * i:flw + 18 + 8 * node_count + 2 * i])[0]
               for i in range(branch_count)]
    branches = [n for n in nodes if n[0] == 2]
    conditions = {n[2] for n in branches}
    check(conditions <= set(range(14)), f"US branch conditions are 0-13 (found {sorted(conditions)})")
    check(14 not in conditions, "no Korean-numbered Astro Galaxy branch (14)")
    astro = [n[3] for n in branches if n[2] == 11]
    check(len(astro) > 50 and max(astro) <= 27, "condition 11 takes Astro Galaxy flag indices 0-27")
    check({12, 13, 18} <= set(astro), "including the Luigi (12, 13) and supply (18) flags")

    # The Luma at the Fountain's Launch Star: AstroGalaxy_Tico041 branches on flag 2
    # (Grand Star 2): true "Step here to go to the Fountain.", false "You can't go up there yet."
    info = sections[b"INF1"]
    data = sections[b"DAT1"]
    count, size = struct.unpack(">HH", bmg[info + 8:info + 12])

    def text(index):
        offset = struct.unpack(">I", bmg[info + 16 + index * size:info + 20 + index * size])[0]
        out, j, raw = [], 0, bmg[data + 8 + offset:]
        while True:
            c = struct.unpack(">H", raw[j:j + 2])[0]
            if c == 0:
                return "".join(out)
            if c == 0x1A:
                j += raw[j + 2]
                continue
            out.append(chr(c))
            j += 2

    table = stage_edit.Table(files["messageid.tbl"])
    ids = {table.get(r, "MessageId"): table.get(r, "Index") for r in range(len(table.rows))}
    luma = ids["AstroGalaxy_Tico041"]
    start = next(i for i, n in enumerate(nodes) if n[0] == 1 and n[2] == luma)
    branch = nodes[nodes[start][3]]
    check(branch[0] == 2 and branch[2] == 11 and branch[3] == 2, "Tico041 branches on Astro Galaxy flag 2")
    true_text = text(nodes[targets[branch[4]]][2])
    false_text = text(nodes[targets[branch[4] + 1]][2])
    check("Fountain" in true_text and "can't go up there" in false_text, "Fountain Luma's two answers")
    print(f"{checks} talk branch data checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
