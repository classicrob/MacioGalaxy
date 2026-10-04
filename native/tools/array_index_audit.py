#!/usr/bin/env python3
"""Audit for fixed-size arrays indexed by a computed value across native/** (not src/).

The class behind the F1 -> Mods crash: `ViewItem items[8]` was written at `items[mModCount + 3]`, so five
mods wrote items[8] and overwrote the next member. Neither the compiler nor ASan sees that inside a struct.

How it works: every translation unit under native/ in build/<dir>/compile_commands.json is re-run with
`-fsyntax-only -Xclang -ast-dump`; the textual AST is streamed and every ArraySubscriptExpr whose base is an
array of known size and whose location is in native/ is classified by its index:

  CONST-OOB   a compile-time constant index >= the array size (a plain bug)
  COMPUTED    the index is arithmetic (`+`, `-`, `*`, `<<`) on a runtime value: `items[count + 2]`.
              Reviewed one by one: each needs a bound the reader can see, or an `audit-ok:` note.
  CALL        the index is a function result (`rows[mBackend->row()]`)
  VAR         a plain variable or member (`lines[i]`). Mostly loop counters; counted, listed with --all.

A line (or the line above it) with `audit-ok: <why>` silences the finding; the reason is the point, so
the comment has to say where the bound comes from ("count clamped to kMaxMods in setMods"). Files whose owner
did not add the comment are listed in array_index_audit_allow.txt as `path :: source text :: why`.

usage: native/tools/array_index_audit.py --build build/macos-gx [--all] [--jobs 4] [--filter home_menu]
                                          [--json out.json] [--tests]
Run it under the build lock like any heavy job:
  build/locked-build.sh <lane> python3 native/tools/array_index_audit.py --build build/macos-gx
Exit status 1 when an unsuppressed CONST-OOB or COMPUTED finding exists.
"""
import argparse
import json
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOC = re.compile(r"<(?P<inner>[^>]*)>")
NODE = re.compile(r"^(?P<prefix>[\s|`\-]*)(?P<kind>[A-Za-z]+)\s")
DIM = re.compile(r"\[(\d+)\]")


def tokens(inner):
    """The location tokens of `<begin, end>` as (file|None, line|None) in print order."""
    out = []
    for part in inner.split(", "):
        part = part.strip()
        if part.startswith("line:"):
            out.append((None, int(part.split(":")[1])))
        elif part.startswith("col:") or "invalid sloc" in part or part.startswith("scratch"):
            out.append((None, None))
        else:
            pieces = part.rsplit(":", 2)  # path:line:col
            try:
                out.append((pieces[0], int(pieces[1])))
            except (IndexError, ValueError):
                out.append((None, None))
    return out


def compile_args(entry):
    args = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    out, skip = [], False
    for i, a in enumerate(args):
        if skip:
            skip = False
            continue
        if a in ("-o", "-MT", "-MF", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-MD", "-MMD") or a.startswith("-fdiagnostics-color") or a.startswith("-fcolor"):
            continue
        out.append(a)
    return out + ["-fsyntax-only", "-fno-color-diagnostics", "-Wno-everything", "-Xclang", "-ast-dump"]


def drop_sizeof(lines):
    """The index lines without the operand subtrees of sizeof/alignof (constant: `buf[sizeof(buf) - 1]`)."""
    out, cut = [], None
    for raw in lines:
        m = NODE.match(raw)
        depth = len(m.group("prefix")) if m else 0
        if cut is not None and depth > cut:
            continue
        cut = None
        out.append(raw)
        if m and m.group("kind") == "UnaryExprOrTypeTraitExpr":
            cut = depth
    return out


def classify(base_lines, index_lines):
    index_lines = drop_sizeof(index_lines)
    base_text = " ".join(base_lines)
    m = DIM.search(base_text)
    if not m:
        return None  # pointer or unknown-size base
    size = int(m.group(1))
    kinds = [NODE.match(l).group("kind") for l in index_lines if NODE.match(l)]
    text = "\n".join(index_lines)
    # A compile-time constant: literals / enum constants / ConstantExpr only.
    if all(k in ("IntegerLiteral", "ConstantExpr", "ImplicitCastExpr", "CXXBoolLiteralExpr", "CharacterLiteral", "ParenExpr",
                 "UnaryOperator", "BinaryOperator", "CStyleCastExpr", "CXXStaticCastExpr") for k in kinds) and \
            "DeclRefExpr" not in text.replace("EnumConstant", "") and "MemberExpr" not in text and "CallExpr" not in text:
        values = [int(v) for v in re.findall(r"IntegerLiteral [^']*'[^']*' (-?\d+)", text)]
        if values and len(index_lines) <= 3 and values[0] >= size:
            return ("CONST-OOB", size)
        return ("CONST", size)
    if "ConstantExpr" in kinds and ("value: Int" in text) and not re.search(r"DeclRefExpr[^\n]*(Var|ParmVar|Field)", text):
        v = re.search(r"value: Int (-?\d+)", text)
        if v and int(v.group(1)) >= size:
            return ("CONST-OOB", size)
        return ("CONST", size)
    if "UnaryExprOrTypeTraitExpr" in text and not re.search(r"DeclRefExpr[^\n]*(Var|ParmVar)|MemberExpr|CallExpr", text):
        return ("CONST", size)
    if re.search(r"BinaryOperator[^\n]*'(\+|-|\*|<<)'", text) and re.search(r"DeclRefExpr[^\n]*(Var|ParmVar)|MemberExpr|CallExpr", text):
        return ("COMPUTED", size)
    if "CallExpr" in kinds or "CXXMemberCallExpr" in kinds:
        return ("CALL", size)
    return ("VAR", size)


def scan(entry, want_tests):
    path = Path(entry["file"])
    args = compile_args(entry)
    try:
        proc = subprocess.run(args, cwd=entry["directory"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                              errors="replace", timeout=900)
    except subprocess.TimeoutExpired:
        return path, [], "timeout"
    findings = []
    cur_file, cur_line = None, 0
    stack = None  # (depth, file, line, lines)
    for raw in proc.stdout.splitlines():
        m = NODE.match(raw)
        if not m:
            continue
        depth = len(m.group("prefix"))
        kind = m.group("kind")
        loc = LOC.search(raw)
        node_file, node_line = cur_file, cur_line
        if loc:
            toks = tokens(loc.group("inner"))
            for i, (f, l) in enumerate(toks):
                if f is not None:
                    cur_file = f
                if l is not None:
                    cur_line = l
                if i == 0:
                    node_file, node_line = cur_file, cur_line
        if stack is not None and depth <= stack[0]:
            findings.append(stack)
            stack = None
        if stack is not None:
            stack[3].append((depth, raw))
        if kind == "ArraySubscriptExpr" and node_file and f"{ROOT}/native/" in str(Path(node_file).resolve()
                                                                                  if node_file.startswith(".") else node_file):
            if want_tests or "/native/tests/" not in node_file:
                if stack is not None:
                    findings.append(stack)
                stack = (depth, node_file, node_line, [(depth, raw)])
    if stack is not None:
        findings.append(stack)
    out = []
    for depth, f, line, lines in findings:
        # children of the subscript: first = base, second = index (depth = prefix width, children are deeper by 2)
        child_depth = min((d for d, _ in lines[1:]), default=None)
        if child_depth is None:
            continue
        children, current = [], None
        for d, raw in lines[1:]:
            if d == child_depth:
                current = []
                children.append(current)
            if current is not None:
                current.append(raw)
        if len(children) < 2:
            continue
        result = classify(children[0], children[1])
        if result is None:
            continue
        kind, size = result
        base_type = re.findall(r"'([^']*\[\d+\][^']*)'", " ".join(children[0]))
        out.append({"file": f, "line": line, "class": kind, "size": size, "type": base_type[0] if base_type else "?"})
    return path, out, None


ALLOW = Path(__file__).with_name("array_index_audit_allow.txt")


def allowlist():
    """`path :: source text :: why` lines for findings in files the auditor does not own (so no comment is
    added there). Keyed by text, not line number, so edits elsewhere in the file do not break it."""
    allowed = {}
    if ALLOW.exists():
        for raw in ALLOW.read_text().splitlines():
            if raw.strip() and not raw.startswith("#"):
                path, text, why = [x.strip() for x in raw.split(" :: ", 2)]
                allowed[(path, text)] = why
    return allowed


def source_line(file, line):
    try:
        lines = Path(file).read_text(errors="replace").splitlines()
        return lines[line - 1].strip(), (lines[line - 2].strip() if line >= 2 else "")
    except Exception:
        return "", ""


def selftest():
    fixture = ROOT / "native/tools/array_index_audit_fixture.cpp"
    entry = {"directory": str(ROOT), "file": str(fixture), "arguments": ["c++", "-std=c++17", "-c", str(fixture)]}
    _, found, err = scan(entry, True)
    got = sorted((r["class"], r["line"]) for r in found)
    lines = fixture.read_text().splitlines()
    want = sorted((cls, n + 1) for n, l in enumerate(lines) for cls in ("COMPUTED", "CONST-OOB", "VAR") if f"EXPECT {cls}" in l or
                  (cls == "VAR" and "VAR: counted" in l))
    ok = [g for g in got if g[0] != "CONST"] == want
    print("selftest", "PASS" if ok else f"FAIL\n  got  {got}\n  want {want}")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", type=Path, default=ROOT / "build/macos-gx")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--filter", default="", help="only translation units whose path contains this")
    ap.add_argument("--all", action="store_true", help="also list CALL and VAR findings")
    ap.add_argument("--tests", action="store_true", help="include native/tests")
    ap.add_argument("--json", type=Path)
    ap.add_argument("--selftest", action="store_true", help="check the detector against array_index_audit_fixture.cpp")
    args = ap.parse_args()
    if args.selftest:
        return selftest()

    db = json.loads((args.build / "compile_commands.json").read_text())
    root_native = f"{ROOT}/native/"
    entries, seen = [], set()
    for e in db:
        f = str(Path(e["directory"], e["file"]).resolve()) if not e["file"].startswith("/") else e["file"]
        if root_native in f and f not in seen and args.filter in f and (args.tests or "/native/tests/" not in f):
            seen.add(f)
            e = dict(e, file=f)
            entries.append(e)
    print(f"auditing {len(entries)} translation units under native/", file=sys.stderr)

    results, failed = {}, []
    with ThreadPoolExecutor(args.jobs) as pool:
        for path, found, err in pool.map(lambda e: scan(e, args.tests), entries):
            if err:
                failed.append((str(path), err))
            for r in found:
                results[(r["file"], r["line"], r["class"], r["type"])] = r  # a header is seen from many TUs

    rows = sorted(results.values(), key=lambda r: (r["class"], r["file"], r["line"]))
    allowed = allowlist()
    counts = {}
    shown = []
    for r in rows:
        text, above = source_line(r["file"], r["line"])
        r["source"] = text
        rel_path = str(Path(r["file"]).relative_to(ROOT)) if str(r["file"]).startswith(str(ROOT)) else r["file"]
        r["suppressed"] = "audit-ok:" in text or "audit-ok:" in above or (rel_path, text) in allowed
        key = (r["class"], "suppressed" if r["suppressed"] else "open")
        counts[key] = counts.get(key, 0) + 1
        if r["class"] in ("CONST-OOB", "COMPUTED") or args.all:
            shown.append(r)
    for r in shown:
        if r["class"] in ("CONST", ) and not args.all:
            continue
        rel = str(Path(r["file"]).relative_to(ROOT)) if str(r["file"]).startswith(str(ROOT)) else r["file"]
        mark = "ok " if r["suppressed"] else "   "
        print(f"{mark}{r['class']:<9} {rel}:{r['line']}  [{r['type']}]  {r['source'][:110]}")
    print("\nsummary (class, state): count")
    for key in sorted(counts):
        print(f"  {key[0]:<9} {key[1]:<10} {counts[key]}")
    for f, e in failed:
        print(f"  FAILED {f}: {e}", file=sys.stderr)
    if args.json:
        args.json.write_text(json.dumps(rows, indent=1))
    bad = [r for r in rows if r["class"] in ("CONST-OOB", "COMPUTED") and not r["suppressed"]]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
