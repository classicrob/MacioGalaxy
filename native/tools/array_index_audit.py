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

Third-party sources under native/**/vendor/ (for example the editor's meshoptimizer) are not audited.
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
import functools
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@functools.lru_cache(maxsize=None)
def real(path):
    """The real path: compile_commands.json and clang print the path the build was configured with, which
    differs from ROOT (resolved) when the checkout is reached through a symlink (a moved or renamed checkout)."""
    return os.path.realpath(path)


LOC = re.compile(r"<(?P<inner>[^>]*)>")
NODE = re.compile(r"^(?P<prefix>[\s|`\-]*)(?P<kind>[A-Za-z]+:?)\s")  # also `value: Int 6` lines of constant initialisers
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
                out.append((real(pieces[0]), int(pieces[1])))
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
    # The array's own type (`char[3][96]`), not the decayed pointer's (`char (*)[96]`): the first
    # dimension is the one being indexed.
    m = re.search(r"'(?:const |volatile )*[A-Za-z_][^'()*]*?\[(\d+)\]", base_text)
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


def loop_bound(for_lines, name, values=None):
    """The bound of `for (... name = ...; name < BOUND; ...)`: ("const", value|None) for literals and sizeof,
    ("named", names) for a named constant, ("runtime", names) for a variable or member; None if the loop's
    header does not have that shape."""
    # init: a VarDecl of this name near the top of the ForStmt
    if not any(re.search(r"VarDecl [^\n]*\b" + re.escape(name) + r"\b", l) for _, l in for_lines[:6]):
        return None
    for i, (d, raw) in enumerate(for_lines):
        if re.search(r"BinaryOperator [^\n]*'(<|<=|!=)'", raw):
            sub = [x for x in for_lines[i + 1:]]
            kids, cur = [], None
            cd = None
            for dd, rr in sub:
                if dd <= d:
                    break
                if cd is None:
                    cd = dd
                if dd == cd:
                    cur = []
                    kids.append(cur)
                cur.append(rr)
            if len(kids) < 2:
                return None
            rhs = kids[1]
            text = "\n".join(rhs)
            if re.search(r"MemberExpr|CallExpr|ParmVar", text) or re.search(r"DeclRefExpr[^\n]*'(?!const )[^']*' lvalue Var", text) and \
                    not re.search(r"'const [^']*'", text):
                names = re.findall(r"MemberExpr [^\n]*?(?:->|\.)(\w+) 0x", text) + re.findall(r"DeclRefExpr[^\n]*(?:Var|ParmVar) 0x[0-9a-f]+ '([^']+)'", text)
                calls = re.findall(r"(?:CXXMemberCallExpr|CallExpr)", text)
                return ("runtime", names or (["<call>"] if calls else ["?"]))
            if "UnaryExprOrTypeTraitExpr" in text:
                return ("sizeof", None)
            enum = re.search(r"DeclRefExpr[^\n]*EnumConstant (0x[0-9a-f]+) '([^']+)'", text)
            if enum and values and enum.group(1) in values and not re.search(r"MemberExpr|CallExpr", text):
                return ("named", [enum.group(2)], values[enum.group(1)])
            if not re.search(r"MemberExpr|CallExpr|DeclRefExpr", text) and re.search(r"value: Int (-?\d+)", text):
                return ("const", int(re.search(r"value: Int (-?\d+)", text).group(1)))  # sizeof(x) / sizeof(x[0]) and std::size(x): the array's own size, not compared
            if re.search(r"DeclRefExpr[^\n]*Var", text):
                if "ConstantExpr" in text and re.search(r"value: Int (-?\d+)", text):
                    return ("const", int(re.search(r"value: Int (-?\d+)", text).group(1)))
                names = re.findall(r"DeclRefExpr[^\n]*Var 0x[0-9a-f]+ '([^']+)'", text)
                addr = re.search(r"DeclRefExpr[^\n]*Var (0x[0-9a-f]+) '", text)
                if addr and values and addr.group(1) in values:
                    return ("named", names or ["?"], values[addr.group(1)])
                return ("named", names or ["?"], None)
            v = re.search(r"IntegerLiteral [^']*'[^']*' (-?\d+)", text) or re.search(r"value: Int (-?\d+)", text)
            return ("const", int(v.group(1)) if v else None)
    return None


def scan(entry, want_tests):
    path = Path(entry["file"])
    args = compile_args(entry)
    try:
        proc = subprocess.run(args, cwd=entry["directory"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                              errors="replace", timeout=900)
    except subprocess.TimeoutExpired:
        return path, [], "timeout"
    finished = []  # closed subscripts: (file, line, lines, for_loops)
    enum_next = [0]  # the value the next implicitly-numbered enumerator takes
    values = {}  # address of a constant VarDecl -> its integer value (to resolve `i < kMaxFoo`)
    cur_file, cur_line = None, 0
    open_nodes = []  # entries: dict(kind, depth, file, line, lines, fors)
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
        if kind == "EnumDecl":
            enum_next[0] = 0
        while open_nodes and depth <= open_nodes[-1]["depth"]:
            n = open_nodes.pop()
            if n["kind"] == "ArraySubscriptExpr":
                finished.append(n)
            elif n["kind"] == "EnumConstantDecl":
                a = re.search(r"EnumConstantDecl (0x[0-9a-f]+) ", n["lines"][0][1])
                v = re.search(r"value: Int (-?\d+)", "\n".join(r for _, r in n["lines"][1:4]))
                enum_next[0] = int(v.group(1)) if v else enum_next[0]  # implicit enumerators count up from the last
                if a:
                    values[a.group(1)] = enum_next[0]
                enum_next[0] += 1
            elif n["kind"] == "VarDecl":
                head = n["lines"][0][1]
                v = re.search(r"value: Int (-?\d+)", "\n".join(r for _, r in n["lines"][1:4])) or \
                    re.search(r"IntegerLiteral [^']*'[^']*' (-?\d+)", "\n".join(r for _, r in n["lines"][1:4]))
                a = re.search(r"VarDecl (0x[0-9a-f]+) ", head)
                if v and a and re.search(r"'const [^']*'|constexpr", head):
                    values[a.group(1)] = int(v.group(1))
        for n in open_nodes:
            n["lines"].append((depth, raw))
        interesting = kind == "ForStmt" or (kind == "VarDecl" and ("const" in raw)) or kind == "EnumConstantDecl" or (kind == "ArraySubscriptExpr" and node_file and
                                            f"{ROOT}/native/" in str(Path(node_file).resolve() if node_file.startswith(".") else node_file)
                                            and (want_tests or "/native/tests/" not in node_file))
        if interesting:
            fors = [list(n["lines"]) for n in open_nodes if n["kind"] == "ForStmt"] if kind == "ArraySubscriptExpr" else []
            open_nodes.append({"kind": kind, "depth": depth, "file": node_file, "line": node_line, "lines": [(depth, raw)], "fors": fors})
    finished.extend(n for n in open_nodes if n["kind"] == "ArraySubscriptExpr")
    out = []
    for n in finished:
        depth, f, line, lines = n["depth"], n["file"], n["line"], n["lines"]
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
        base_type = re.findall(r"'((?:const |volatile )*[A-Za-z_][^'()*]*?\[\d+\][^'()*]*)'", " ".join(children[0]))
        row = {"file": f, "line": line, "class": kind, "size": size, "type": base_type[0] if base_type else "?"}
        if kind == "VAR":
            var = re.search(r"DeclRefExpr[^\n]*Var 0x[0-9a-f]+ '([^']+)'", "\n".join(children[1]))
            row["loop"] = None
            row["array_size"] = size
            if var and "MemberExpr" not in "".join(children[1]):
                for for_lines in reversed(n["fors"]):  # innermost first
                    bound = loop_bound(for_lines, var.group(1), values)
                    if bound:
                        row["loop"] = bound
                        break
        out.append(row)
    return path, out, None


ALLOW = Path(os.environ.get("PETARI_AUDIT_ALLOW") or Path(__file__).with_name("array_index_audit_allow.txt"))  # env: tests only


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


def guard_hint(file, line, bound):
    """Is the loop's bound clamped or asserted in the 80 source lines above? (a hint for review, not a proof)"""
    try:
        lines = Path(file).read_text(errors="replace").splitlines()
    except Exception:
        return False
    window = "\n".join(lines[max(0, line - 81):line])
    for name in bound:
        if name in ("?", "<call>"):
            continue
        n = re.escape(name)
        if re.search(rf"(min|clamp)\s*\([^;]*\b{n}\b|\b{n}\b\s*=\s*std::(min|clamp)|\b{n}\b[^;\n]*(min|clamp)\s*\(|"
                     rf"\b{n}\b\s*(<|<=)\s*k\w+|k\w+\s*(>|>=)\s*\b{n}\b|assert[^;]*\b{n}\b|\b{n}\b\s*=\s*[^;]*\bk[A-Z]\w+", window):
            return True
    return False


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
                  (cls == "VAR" and ("VAR: counted" in l or "EXPECT LOOP-" in l)))
    ok = [g for g in got if g[0] != "CONST"] == want
    # the loop classes of the VAR findings
    for tag, kind in (("LOOP-RUNTIME", "runtime"), ("LOOP-CONST", "const")):
        for n, l in enumerate(lines):
            if f"EXPECT {tag}" in l:
                row = [r for r in found if r["line"] == n + 1 and r["class"] == "VAR"]
                ok = ok and bool(row) and bool(row[0].get("loop")) and row[0]["loop"][0] == kind
    print("selftest", "PASS" if ok else f"FAIL\n  got  {got}\n  want {want}")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", type=Path, default=ROOT / "build/macos-gx")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--filter", default="", help="only translation units whose path contains this")
    ap.add_argument("--all", action="store_true", help="also list CALL and VAR findings")
    ap.add_argument("--tests", action="store_true", help="include native/tests")
    ap.add_argument("--loops", action="store_true", help="list loop-indexed hits whose runtime bound has no clamp nearby")
    ap.add_argument("--json", type=Path)
    ap.add_argument("--selftest", action="store_true", help="check the detector against array_index_audit_fixture.cpp")
    args = ap.parse_args()
    if args.selftest:
        return selftest()

    if not (args.build / "compile_commands.json").exists():
        print(f"array_index_audit: no {args.build}/compile_commands.json; skipping", file=sys.stderr)
        return 77  # ctest SKIP_RETURN_CODE
    db = json.loads((args.build / "compile_commands.json").read_text())
    root_native = f"{ROOT}/native/"
    entries, seen = [], set()
    for e in db:
        f = real(str(Path(e["directory"], e["file"])))
        if root_native in f and f not in seen and args.filter in f and (args.tests or "/native/tests/" not in f):
            seen.add(f)
            e = dict(e, file=f)
            entries.append(e)
    print(f"auditing {len(entries)} translation units under native/", file=sys.stderr)
    if not entries and not args.filter:
        # A gate that scanned nothing must not pass (the compile database's paths did not match ROOT).
        print(f"array_index_audit: 0 translation units under {root_native} in {args.build}/compile_commands.json; "
              "refusing to pass an audit that scanned nothing", file=sys.stderr)
        return 2

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
        vendored = "/vendor/" in "/" + rel_path  # third-party sources (native/**/vendor/): not ours, not audited
        r["suppressed"] = vendored or "audit-ok:" in text or "audit-ok:" in above or (rel_path, text) in allowed
        loop = r.get("loop")
        sub = r["class"]
        if r["class"] == "VAR":
            if not loop:
                sub = "VAR/other"
            elif loop[0] == "runtime":
                r["hint"] = guard_hint(r["file"], r["line"], loop[1])
                sub = "VAR/loop-runtime/" + ("guard-hint" if r["hint"] else "NO-HINT")
            elif loop[0] == "sizeof":
                sub = "VAR/loop-own-size"
            else:
                value = loop[2] if loop[0] == "named" else loop[1]
                if value is None:
                    sub = "VAR/loop-bound-UNRESOLVED"
                elif value <= r["array_size"]:  # `i < value` never passes the end: proven
                    sub = "VAR/loop-proven"
                else:
                    sub = "VAR/loop-bound-OVER-SIZE"
        r["sub"] = sub
        key = (sub, "suppressed" if r["suppressed"] else "open")
        counts[key] = counts.get(key, 0) + 1
        if r["class"] in ("CONST-OOB", "COMPUTED") or args.all or (args.loops and r["sub"] in ("VAR/loop-runtime/NO-HINT", "VAR/loop-bound-UNRESOLVED", "VAR/loop-bound-OVER-SIZE")):
            shown.append(r)
    for r in shown:
        if r["class"] in ("CONST", ) and not args.all:
            continue
        rel = str(Path(r["file"]).relative_to(ROOT)) if str(r["file"]).startswith(str(ROOT)) else r["file"]
        mark = "ok " if r["suppressed"] else "   "
        lp = r.get("loop")
        loop = ""
        if lp:
            what = "/".join(lp[1]) if isinstance(lp[1], list) else lp[1]
            loop = f"  (loop < {what}{'=' + str(lp[2]) if len(lp) > 2 and lp[2] is not None else ''}; array {r.get('array_size')})"
        print(f"{mark}{r['class']:<9} {rel}:{r['line']}  [{r['type']}]  {r['source'][:110]}{loop}")
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
