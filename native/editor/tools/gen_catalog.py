#!/usr/bin/env python3
"""Generate native/editor/catalog/generated.json: what each placeable class reads, from the decompilation.

For every entry of NameObjFactory's create table (src/Game/NameObj/NameObjFactory.cpp) it finds the
class's source (and its base classes', two levels) and records:
  - args:     Obj_argN reads (MR::getJMapInfoArgN*), with the type of the variable they fill (int, float, bool)
  - switches: MR::useStageSwitch{Read,Write}{A,B,Appear,Dead,...} / needStageSwitch* / listenStageSwitch*
  - links:    whether it can ride a rail (initRailRider / RailRider; required-ness comes from the class sweep);
              GroupId, ClippingGroupId, DemoGroupId, CameraSetId, MessageId reads
  - archives: the create table's archive and cName2ArchiveNamesTable's
  - category: from the source directory
Names, help and enum labels are curated separately (native/editor/catalog/curated.json); the class
sweep (docs/dev/level-editor/catalog/sweep.json) proves required links live. Our source only; no disc.

usage: python3 native/editor/tools/gen_catalog.py   (from the repository root)
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FACTORY = ROOT / "src/Game/NameObj/NameObjFactory.cpp"

CATEGORY_BY_DIR = {
    "Enemy": "enemy", "Boss": "enemy", "NPC": "npc", "Ride": "launcher", "AreaObj": "area", "Gravity": "gravity",
    "Camera": "camera", "Map": "planet", "Demo": "logic", "MapObj": "other", "Player": "other", "Screen": "other",
}

SWITCH_CALLS = {
    "useStageSwitchReadA": ("SW_A", "read"), "useStageSwitchWriteA": ("SW_A", "write"),
    "useStageSwitchReadB": ("SW_B", "read"), "useStageSwitchWriteB": ("SW_B", "write"),
    "useStageSwitchReadAppear": ("SW_APPEAR", "read"), "useStageSwitchWriteDead": ("SW_DEAD", "write"),
    "useStageSwitchReadParam": ("SW_PARAM", "read"), "useStageSwitchSleep": ("SW_SLEEP", "read"),
    "needStageSwitchReadA": ("SW_A", "read"), "needStageSwitchWriteA": ("SW_A", "write"),
    "needStageSwitchReadB": ("SW_B", "read"), "needStageSwitchWriteB": ("SW_B", "write"),
    "needStageSwitchReadAppear": ("SW_APPEAR", "read"), "needStageSwitchWriteDead": ("SW_DEAD", "write"),
    "listenStageSwitchOnOffA": ("SW_A", "read"), "listenStageSwitchOnOffB": ("SW_B", "read"),
    "listenStageSwitchOnA": ("SW_A", "read"), "listenStageSwitchOnB": ("SW_B", "read"),
    "listenStageSwitchOnOffAppear": ("SW_APPEAR", "read"), "listenStageSwitchOnAppear": ("SW_APPEAR", "read"),
}
LINK_FIELDS = {"GroupId": "group", "ClippingGroupId": "group", "DemoGroupId": "demo", "CameraSetId": "camera",
               "MessageId": "message", "CastId": "cast"}


def strip_comments(text):
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def entries(text, table):
    """The {...} items of a table array in the factory source."""
    start = text.index(table)
    body = text[text.index("{", start) + 1:]
    depth, items, current = 0, [], ""
    for ch in body:
        if ch == "{":
            depth += 1
            if depth == 1:
                current = ""
                continue
        elif ch == "}":
            if depth == 0:
                break
            depth -= 1
            if depth == 0:
                items.append(current)
                continue
        if depth >= 1:
            current += ch
    return [[part.strip() for part in item.split(",")] for item in items]


def index_sources():
    by_class = {}
    for path in (ROOT / "src/Game").rglob("*.cpp"):
        by_class.setdefault(path.stem, []).append(path)
    headers = {}
    for path in (ROOT / "include/Game").rglob("*.hpp"):
        headers.setdefault(path.stem, []).append(path)
    return by_class, headers


def class_of(creator):
    m = re.search(r"<\s*([A-Za-z_]\w*)\s*>", creator)
    if m:
        return m.group(1)
    m = re.search(r"create(\w+)", creator)
    return m.group(1) if m else None


def bases(cls, headers):
    out = []
    for path in headers.get(cls, []):
        m = re.search(rf"class\s+{cls}\s*:\s*public\s+([A-Za-z_]\w*)", path.read_text(errors="replace"))
        if m:
            out.append(m.group(1))
    return out


def var_type(var, texts):
    name = re.escape(var.split("->")[-1].split(".")[-1].lstrip("&"))
    for text in texts:
        m = re.search(rf"\b(s32|f32|bool|int|float|u32|s16|u16|s8|u8|long)\s+\*?{name}\b", text)
        if m:
            return {"f32": "float", "float": "float", "bool": "bool"}.get(m.group(1), "int")
    return "int"


def scan(cls, by_class, headers, depth=0, seen=None):
    seen = seen if seen is not None else set()
    if cls in seen or depth > 2:
        return {"args": {}, "switches": set(), "rail": None, "links": set(), "dir": None}
    seen.add(cls)
    texts = [strip_comments(p.read_text(errors="replace")) for p in by_class.get(cls, [])]
    htexts = [strip_comments(p.read_text(errors="replace")) for p in headers.get(cls, [])]
    result = {"args": {}, "switches": set(), "rail": None, "links": set(),
              "dir": by_class[cls][0].parent.name if by_class.get(cls) else None}
    for text in texts:
        for m in re.finditer(r"getJMapInfoArg([0-7])(NoInit|WithInit)?\s*\(\s*\w+\s*,\s*([^)]+)\)", text):
            index, var = int(m.group(1)), m.group(3).strip()
            result["args"].setdefault(index, var_type(var, texts + htexts))
        for call, sw in SWITCH_CALLS.items():
            if re.search(rf"\b{call}\s*\(", text):
                result["switches"].add(sw)
        if re.search(r"\binitRailRider\s*\(|\bnew\s+RailRider\s*\(", text):
            guarded = re.search(r"\bisConnectedWithRail\s*\(", text) is not None
            result["rail"] = "optional" if guarded or result["rail"] == "optional" else "required"
        for field, kind in LINK_FIELDS.items():
            if re.search(rf'"{field}"|getJMapInfo{field}|{field}\s*\(', text):
                result["links"].add((field, kind))
        if re.search(r"getJMapInfoGroupId|joinToGroup|initDefaultPosAndGroup|MR::joinToGroupArray", text):
            result["links"].add(("GroupId", "group"))
        if re.search(r"getJMapInfoMessageId|initTalkCtrl|createTalkCtrl", text):
            result["links"].add(("MessageId", "message"))
    for base in bases(cls, headers):
        inherited = scan(base, by_class, headers, depth + 1, seen)
        for index, kind in inherited["args"].items():
            result["args"].setdefault(index, kind)
        result["switches"] |= inherited["switches"]
        result["links"] |= inherited["links"]
        if result["rail"] is None:
            result["rail"] = inherited["rail"]
        result["dir"] = result["dir"] or inherited["dir"]
    return result


def main():
    text = strip_comments(FACTORY.read_text(errors="replace"))
    archives = {}
    for item in entries(text, "cName2ArchiveNamesTable[]"):
        if len(item) >= 2:
            archives.setdefault(item[0].strip('"'), []).extend(a.strip('"') for a in item[1:] if a.startswith('"'))
    by_class, headers = index_sources()
    out = []
    for item in entries(text, "cCreateTable[]"):
        if len(item) < 2 or not item[0].startswith('"'):
            continue
        name, creator = item[0].strip('"'), item[1]
        archive = item[2].strip('"') if len(item) > 2 and item[2].startswith('"') else None
        cls = class_of(creator)
        info = scan(cls, by_class, headers) if cls else {"args": {}, "switches": set(), "rail": None, "links": set(), "dir": None}
        links = [{"kind": kind, "field": field, "required": False} for field, kind in sorted(info["links"])]
        if info["rail"]:
            # A static scan can't tell a rail the class needs from one it uses when present (helpers and
            # base classes guard it elsewhere): "required" comes only from the class sweep's live runs.
            links.insert(0, {"kind": "rail", "field": "CommonPath_ID", "required": False})
        out.append({
            "name": name,
            "class": cls,
            "category": CATEGORY_BY_DIR.get(info["dir"] or "", "other"),
            "args": [{"index": i, "type": info["args"][i]} for i in sorted(info["args"])],
            "switches": [{"field": f, "role": r} for f, r in sorted(info["switches"])],
            "links": links,
            "archives": sorted({a for a in ([archive] if archive else []) + archives.get(name, [])}),
        })
    # The class sweep's live evidence (docs/dev/level-editor/catalog/sweep.json, when present): a link
    # the bisect found required, and the overall verdict.
    sweep_path = ROOT / "docs/dev/level-editor/catalog/sweep.json"
    if sweep_path.is_file():
        sweep = {c.get("class"): c for c in json.loads(sweep_path.read_text()).get("classes", [])}
        for entry in out:
            found = sweep.get(entry["name"])
            if not found:
                continue
            entry["sweep"] = found.get("summary", "untested")
            # safe | suspect | unverified | unsafe | unmeasured (class_sweep.py live_recreate_verdict): the editor falls back
            # to the reload path only for "unsafe".
            entry["live_recreate"] = found.get("live_recreate", "unmeasured")
            required = set(found.get("needs_links") or {})
            for field in sorted(required):
                link = next((l for l in entry["links"] if l["field"] == field), None)
                if link is None:
                    kind = "rail" if field == "CommonPath_ID" else LINK_FIELDS.get(field, "other")
                    entry["links"].append({"kind": kind, "field": field, "required": True})
                else:
                    link["required"] = True
    # Where each class is placed on the disc (gen_class_tables.py): the table the editor adds new rows to.
    tables_path = ROOT / "native/editor/catalog/tables.json"
    if tables_path.is_file():
        doc = json.loads(tables_path.read_text())
        tables, collision = doc["tables"], {a.lower() for a in doc.get("collision", [])}
        boxes = {a.lower(): b for a, b in doc.get("collisionBoxes", {}).items()}
        for entry in out:
            if entry["name"] in tables:
                entry["table"] = tables[entry["name"]]
            # Collision when one of its archives (or the name's own) holds a .kcl.
            if any(a.lower() in collision for a in entry["archives"] + [entry["name"]]):
                entry["hasCollision"] = True
            # The collision box: of its own-named archive first, else the first archive with one.
            for a in [entry["name"]] + entry["archives"]:
                if a.lower() in boxes:
                    entry["collisionBox"] = boxes[a.lower()]
                    break
    target = ROOT / "native/editor/catalog/generated.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps(c, separators=(",", ":"), ensure_ascii=False) for c in out]
    target.write_text('{"format":1,"source":"native/editor/tools/gen_catalog.py (decompilation scan)","classes":[\n'
                      + ",\n".join(lines) + "\n]}\n")
    with_args = sum(1 for c in out if c["args"])
    print(f"{len(out)} classes ({with_args} with args, {sum(1 for c in out if c['switches'])} with switches, "
          f"{sum(1 for c in out if any(l['kind'] == 'rail' for l in c['links']))} with a rail) -> {target.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
