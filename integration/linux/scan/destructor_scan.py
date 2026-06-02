#!/usr/bin/env python3
"""destructor_scan.py — whole-tree batch driver for the
destructor-completeness detector.

Walks a kernel tree, enumerates destructor-shaped functions
(functions that bare-`kfree` a struct object), and runs
destructor_completeness.analyze on each.  Sibling sources are
cached per directory so the scan is fast (the analyzer
otherwise re-reads ~160 files per call).

Outputs a CSV of candidates (functions with missing owned
fields) plus a ranked summary.

Usage:
  destructor_scan.py --tree /path/to/linux --out-csv out.csv
                     [--max-files N]
"""
from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import defaultdict
from pathlib import Path

SCAN_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCAN_DIR))
import destructor_completeness as dc  # type: ignore


_SKIP_TOP = {"Documentation", "samples", "tools", "scripts",
             "LICENSES"}
# A function is a destructor candidate if its body bare-frees
# an identifier or arrow/dot chain.  Cheap pre-filter.
_BARE_KFREE_RE = re.compile(
    r"\b(?:kfree|kvfree|kfree_skb)\s*\(\s*"
    r"([A-Za-z_]\w*(?:(?:->|\.)[A-Za-z_]\w*)*)\s*\)\s*;")

# Constructor-shaped names: these bare-`kfree(obj)` on EARLY
# error paths (before sub-fields are allocated), which looks
# like an incomplete destructor but is not a leak.
_CTOR_NAME_RE = re.compile(
    r"(?:^|_)(?:init|alloc|create|new|parse|probe|setup|"
    r"register|add|open|start|build|make|prepare|attach)"
    r"(?:_|$)")

# Destructor-shaped names: a positive signal that the function
# is a teardown routine (so a bare kfree(obj) of its object is
# a real teardown, not an error-path cleanup of a transient
# local).  Requiring this — rather than merely excluding
# constructor names — is essential: neutral-named functions
# (e.g. *_copy_from_user, *_read_ext_csd) routinely kfree a
# temporary buffer on an error path and would otherwise flood
# the candidate set.
_DTOR_NAME_RE = re.compile(
    r"(?:^|_)(?:fini|free|destroy|release|del|delete|remove|"
    r"exit|cleanup|clean|put|stop|close|disconnect|unregister|"
    r"unbind|teardown|uninit|deinit|dealloc|dtor|shutdown|"
    r"drop|kill|purge|flush|dispose)(?:_|$)")


def _is_destructor_name(fn: str) -> bool:
    """True only when the name is destructor-shaped and not
    constructor-shaped."""
    return (_DTOR_NAME_RE.search(fn) is not None
            and _CTOR_NAME_RE.search(fn) is None)


def _enum_dirs(tree: Path):
    """Yield directories under tree (excluding skip-tops),
    each with its list of .c files."""
    for top in sorted(tree.iterdir()):
        if not top.is_dir() or top.name in _SKIP_TOP:
            continue
        # Walk recursively; group .c files by their parent dir.
        by_dir: dict[Path, list[Path]] = defaultdict(list)
        for cf in top.rglob("*.c"):
            if any(p in cf.parts for p in
                   ("selftests", "kunit", "generated")):
                continue
            by_dir[cf.parent].append(cf)
        for d, cfiles in by_dir.items():
            yield d, sorted(cfiles)


def _dir_sources(d: Path) -> dict[str, str]:
    """Read all .c/.h in directory d once.  Returns
    {filename: text}."""
    out: dict[str, str] = {}
    try:
        files = sorted(d.glob("*.c")) + sorted(d.glob("*.h"))
    except OSError:
        return out
    # Bound to keep memory/time sane on huge dirs.
    for f in files[:400]:
        try:
            out[f.name] = f.read_text(errors="replace")
        except OSError:
            pass
    return out


def _build_global_helper_freed(tree: Path,
                               max_files: int | None) -> dict:
    """One cheap pass over the whole tree: for each function,
    record the set of field NAMES it frees via `kfree(v->f)`
    (struct-type-agnostic).  Used to resolve transitive frees
    across directory boundaries — a destructor that delegates
    field teardown to a helper in a different directory (e.g.
    part_release -> hd_free_part in block/blk.h frees
    part->info) must not be flagged.

    Returns {func_name: set(field_names)}."""
    helper_freed: dict[str, set] = defaultdict(set)
    field_free_re = re.compile(
        r"\b(?:" + dc._FREE_APIS + r")\s*\(\s*[A-Za-z_]\w*"
        r"\s*->\s*([A-Za-z_][\w.]*?)\s*\)")
    done = 0
    for top in sorted(tree.iterdir()):
        if not top.is_dir() or top.name in _SKIP_TOP:
            continue
        for cf in list(top.rglob("*.c")) + list(top.rglob("*.h")):
            if any(p in cf.parts for p in
                   ("selftests", "kunit", "generated")):
                continue
            if max_files is not None and done >= max_files:
                return helper_freed
            done += 1
            try:
                text = cf.read_text(errors="replace")
            except OSError:
                continue
            if not field_free_re.search(text):
                continue
            for fname, fbody in dc._enum_functions(text):
                for m in field_free_re.finditer(fbody):
                    helper_freed[fname].add(m.group(1))
    return helper_freed


def _build_dir_index(src_cache: dict[str, str]):
    """One pass over all functions in a directory's sources to
    build the maps the per-candidate analysis needs, so we
    don't re-scan O(candidates x sources).

    Returns:
      func_body[name]       -> body text (last definition wins)
      func_paramtype[name]  -> (struct_type, param_name)
      decl_type[field]      -> struct type T for `struct T *field;`
      sibling_freed[T]      -> set of fields freed by ANY function
                               that bare-frees a `struct T *` var
                               (redundant-destructor evidence;
                               includes the candidate itself,
                               which is harmless since those
                               fields also appear in `freed`).
    """
    func_body: dict[str, str] = {}
    func_paramtype: dict[str, tuple] = {}
    decl_type: dict[str, str] = {}
    sibling_freed: dict[str, set] = defaultdict(set)
    # Count `struct T { ... }` definitions per type name.  When
    # a name has >1 definition in the scanned sources (e.g.
    # `struct workspace` in btrfs zlib.c/lzo.c/zstd.c, or
    # `struct slot` across hotplug drivers), sibling/constructor
    # field evidence is conflated across unrelated structs;
    # such collision-prone types are excluded from candidacy.
    struct_def_count: dict[str, int] = defaultdict(int)

    decl_re = re.compile(r"struct\s+(\w+)\s*\*\s*([A-Za-z_]\w*)\s*;")
    tvar_re = re.compile(r"struct\s+(\w+)\s*\*\s*([A-Za-z_]\w*)")
    structdef_re = re.compile(r"\bstruct\s+(\w+)\s*\{")

    for name, text in src_cache.items():
        for m in structdef_re.finditer(text):
            struct_def_count[m.group(1)] += 1
        for m in decl_re.finditer(text):
            decl_type.setdefault(m.group(2), m.group(1))
        for fname, fbody in dc._enum_functions(text):
            func_body[fname] = fbody
            pt, pn = dc._param_struct_type(text, fname)
            if pt:
                func_paramtype[fname] = (pt, pn)
            tvars: dict[str, str] = {}
            for tm in tvar_re.finditer(fbody):
                tvars[tm.group(2)] = tm.group(1)
            if pt and pn:
                tvars[pn] = pt
            for v, t in tvars.items():
                esc = re.escape(v)
                if not re.search(
                        r"\b(?:" + dc._FREE_APIS + r")\s*\(\s*" + esc
                        + r"\s*\)", fbody):
                    continue
                for fm in re.finditer(
                        r"\b(?:" + dc._FREE_APIS + r")\s*\(\s*" + esc
                        + r"\s*->\s*([A-Za-z_][\w.]*?)\s*\)", fbody):
                    sibling_freed[t].add(fm.group(1))
    return (func_body, func_paramtype, decl_type, sibling_freed,
            struct_def_count)


def _analyze_fast(fname: str, fbody: str, func_body,
                  func_paramtype, decl_type,
                  sibling_freed,
                  global_helper_freed=None) -> dc.DtorVerdict:
    """Per-candidate analysis using precomputed dir maps."""
    bare = dc._bare_kfree_objects(fbody)
    obj_candidates = [b for b in bare
                      if re.fullmatch(r"[A-Za-z_][\w]*"
                                      r"(?:(?:->|\.)[A-Za-z_]\w*)*", b)]
    if not obj_candidates:
        return dc.DtorVerdict(False, None, None)
    prefixes = [o for o in obj_candidates
                if any(x != o and x.startswith(o + "->")
                       for x in obj_candidates)]
    obj = max(prefixes, key=len) if prefixes else max(
        obj_candidates, key=len)

    term = dc._terminal_field(obj)
    if term is not None:
        struct_type = decl_type.get(term)
    else:
        m = re.search(r"struct\s+(\w+)\s*\*\s*" + re.escape(obj)
                      + r"\b", fbody)
        if m:
            struct_type = m.group(1)
        else:
            # Only inherit a parameter's type when the freed
            # object IS that parameter (avoids misattributing
            # a void* arg to an unrelated struct param).
            pt = func_paramtype.get(fname)
            struct_type = pt[0] if (pt and pt[1] == obj) else None
    if struct_type is None:
        return dc.DtorVerdict(False, None, obj)

    freed = dc._field_paths_freed(fbody, [obj])
    base = re.match(r"[A-Za-z_]\w*", obj)
    base_id = base.group(0) if base else obj
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(([^;]*?)\)\s*;", fbody):
        callee, args = m.group(1), m.group(2)
        if callee in ("kfree", "kvfree", "kfree_skb", "if", "for",
                      "while", "switch", "return", "sizeof"):
            continue
        if not re.search(r"\b" + re.escape(base_id) + r"\b", args):
            continue
        cbody = func_body.get(callee)
        if cbody is None:
            continue
        pt = func_paramtype.get(callee)
        tvars = set(re.findall(
            r"struct\s+" + re.escape(struct_type) + r"\s*\*\s*(\w+)",
            cbody))
        if pt and pt[0] == struct_type and pt[1]:
            tvars.add(pt[1])
        for v in tvars:
            esc = re.escape(v)
            for fm in re.finditer(
                    r"\b(?:" + dc._FREE_APIS + r")\s*\(\s*" + esc
                    + r"\s*->\s*([A-Za-z_][\w.]*?)\s*\)", cbody):
                freed.add(fm.group(1))

    owned = set(sibling_freed.get(struct_type, set()))
    for cname in dc._constructor_names(fname):
        cbody = func_body.get(cname)
        if cbody is None:
            continue
        cpt = func_paramtype.get(cname)
        tvars = set(re.findall(
            r"struct\s+" + re.escape(struct_type) + r"\s*\*\s*(\w+)",
            cbody))
        if cpt and cpt[0] == struct_type and cpt[1]:
            tvars.add(cpt[1])
        for v in tvars:
            esc = re.escape(v)
            for am in re.finditer(
                    esc + r"\s*->\s*([A-Za-z_][\w.]*?)\s*=\s*"
                    r"(?:" + dc._ALLOC_APIS + r")\s*\(", cbody):
                owned.add(am.group(1))

    missing = owned - freed
    # Global cross-directory transitive-free pruning: if the
    # destructor calls a helper (anywhere in the tree) that
    # frees the missing field, it is not leaked.  Resolves
    # delegation to helpers defined outside the candidate's
    # own directory (e.g. part_release -> hd_free_part in
    # block/blk.h frees part->info).
    if missing and global_helper_freed is not None:
        called = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", fbody))
        helper_frees: set = set()
        for c in called:
            helper_frees |= global_helper_freed.get(c, set())
        if helper_frees:
            missing = missing - helper_frees
    return dc.DtorVerdict(True, struct_type, obj, freed, owned,
                          missing)


def scan_tree(tree: Path, max_files: int | None) -> list[dict]:
    candidates: list[dict] = []
    files_done = 0
    print("  building global helper-free index ...", flush=True)
    global_helper_freed = _build_global_helper_freed(tree, None)
    print(f"  global helper-free index: "
          f"{len(global_helper_freed)} functions", flush=True)
    for d, cfiles in _enum_dirs(tree):
        src_cache = _dir_sources(d)
        if not src_cache:
            continue
        (func_body, func_paramtype, decl_type,
         sibling_freed, struct_def_count) = _build_dir_index(src_cache)
        for cf in cfiles:
            if max_files is not None and files_done >= max_files:
                return candidates
            files_done += 1
            primary = src_cache.get(cf.name)
            if primary is None:
                continue
            if not _BARE_KFREE_RE.search(primary):
                continue
            for fname, fbody in dc._enum_functions(primary):
                if not _BARE_KFREE_RE.search(fbody):
                    continue
                if not _is_destructor_name(fname):
                    continue
                try:
                    v = _analyze_fast(
                        fname, fbody, func_body, func_paramtype,
                        decl_type, sibling_freed,
                        global_helper_freed)
                except Exception:
                    continue
                # Skip collision-prone struct types (multiple
                # distinct definitions in the dir conflate the
                # field evidence).
                if (v.struct_type
                        and struct_def_count.get(v.struct_type, 0) > 1):
                    continue
                if v.missing_fields:
                    candidates.append({
                        "file": str(cf.relative_to(tree)),
                        "function": fname,
                        "struct": v.struct_type or "",
                        "freed": ",".join(sorted(v.freed_fields)),
                        "owned": ",".join(sorted(v.owned_fields)),
                        "missing": ",".join(sorted(v.missing_fields)),
                        "n_missing": len(v.missing_fields),
                    })
    return candidates


def _confidence(c: dict) -> int:
    """Rank heuristic: fewer missing fields + resolved struct
    => higher confidence (lower sort key)."""
    score = 0
    if not c["struct"]:
        score += 100
    score += c["n_missing"]  # prefer single missing field
    # Penalise generic field names that are often borrowed.
    if any(f in c["missing"].split(",")
           for f in ("parent", "dev", "priv", "data")):
        score += 5
    return score


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", type=Path, required=True)
    ap.add_argument("--out-csv", type=Path, required=True)
    ap.add_argument("--max-files", type=int, default=None)
    args = ap.parse_args(argv)

    print(f"scanning {args.tree} ...", flush=True)
    cands = scan_tree(args.tree, args.max_files)
    cands.sort(key=_confidence)

    args.out_csv.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "file", "function", "struct", "freed", "owned",
            "missing", "n_missing"])
        w.writeheader()
        w.writerows(cands)

    print(f"\ncandidates: {len(cands)}  (CSV: {args.out_csv})")
    print("\n=== top 30 by confidence ===")
    for c in cands[:30]:
        print(f"  [{c['struct']:>22}] {c['file']}:{c['function']} "
              f"MISSING {c['missing']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
