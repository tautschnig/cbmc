"""destructor_completeness.py — cross-function leak detector.

Detects the "incomplete destructor" bug class: a function that
tears down a heap object of struct type T (it calls
`kfree(obj)` on the object itself) but fails to free one of T's
*owned* heap-pointer fields, leaking it.

Two real CVEs motivate this detector — both were missed by the
per-function leak harness because the allocation site is in a
different function (often a different file):

  * CVE-2023-53453 — `radeon_atombios_fini` frees
    `atom_context->scratch` and the struct, but not
    `atom_context->iio`.  `iio` is allocated in `atom.c`
    (`atom_index_iio`) and freed by the sibling destructor
    `atom_destroy`.

  * CVE-2023-53697 — `unregister_nvdimm_pmu` frees `nd_pmu`
    but not `nd_pmu->pmu.attr_groups`, which is allocated in
    `register_nvdimm_pmu`.

Approach (static, cross-function, multi-file):

  1. Parse the destructor D.  Identify the *primary object* O
     — the expression that is itself `kfree`'d — and the set
     of fields `freed_in_D = {f : kfree(O...->f) in D}`.
  2. Resolve the struct type name T of O.
  3. Scan the target file and its sibling .c files in the same
     directory for *owned* fields of T: a field f is owned when
     `V->...->f = <alloc-API>(...)` or `kfree(V->...->f)`
     appears for some variable V of type `struct T *`.
  4. `missing = owned_fields(T) - freed_in_D`.  If D is a full
     destructor (frees O itself) and `missing` is non-empty,
     report a leak candidate naming the missing fields.

The detector is intentionally conservative: it requires D to
free the struct object itself (so it is unambiguously a
destructor, not a mutator), and it only counts a field as
owned when there is direct textual evidence of allocation or
freeing for that field on the same struct type.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path


# Allocation APIs whose result, stored into a struct field,
# makes that field "owned" (heap, must-free).  Mirrors the
# FRESH set in triage_filter — refcount-get APIs are excluded.
_ALLOC_APIS = (
    r"k(?:malloc|zalloc|calloc|malloc_array|memdup|memdup_nul|"
    r"strdup|strndup|asprintf)|"
    r"kv(?:malloc|zalloc|calloc|malloc_array)|"
    r"v(?:malloc|zalloc)|"
    r"alloc_skb|dev_alloc_skb|"
    r"kmem_cache_(?:alloc|zalloc)"
)
_FREE_APIS = r"(?:kfree(?:_skb)?|k?vfree)"


@dataclass
class DtorVerdict:
    is_destructor: bool
    struct_type: str | None
    object_expr: str | None
    freed_fields: set[str] = field(default_factory=set)
    owned_fields: set[str] = field(default_factory=set)
    missing_fields: set[str] = field(default_factory=set)
    reason: str = ""


def _function_body(source: str, fn_name: str) -> str | None:
    """Extract a function body by brace-matching.  Returns the
    text between the opening and closing brace (inclusive of
    neither), or None if not found."""
    # Find the signature: `<ret> <fn_name>(...)` followed by `{`.
    pat = re.compile(
        r"(?:^|\n)[^\n;]*?\b" + re.escape(fn_name) + r"\s*\([^;{]*\)\s*\{")
    m = pat.search(source)
    if not m:
        return None
    start = m.end()  # just after the opening brace
    depth = 1
    i = start
    n = len(source)
    while i < n and depth > 0:
        c = source[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    if depth != 0:
        return None
    return source[start:i - 1]


def _bare_kfree_objects(body: str) -> list[str]:
    """Return the list of expressions E such that `kfree(E)`
    (no trailing `->field` or `[idx]`) appears — i.e. the
    object itself is freed."""
    out: list[str] = []
    for m in re.finditer(
            r"\b(?:" + _FREE_APIS + r")\s*\(\s*([^;]+?)\s*\)\s*;", body):
        arg = m.group(1).strip()
        # Strip a leading cast.
        arg = re.sub(r"^\([^)]*\)\s*", "", arg).strip()
        # Bare object = no array index and not ending in a
        # field access we'd consider a sub-field-free.  We
        # accept dotted/arrow chains (rdev->mode_info.atom_context)
        # but classify field-frees separately below.
        out.append(arg)
    return out


def _terminal_field(expr: str) -> str | None:
    """Return the terminal field name of an arrow/dot chain,
    or None for a bare identifier."""
    m = re.search(r"(?:->|\.)\s*([A-Za-z_]\w*)\s*$", expr)
    return m.group(1) if m else None


def _struct_type_of_terminal(source_files: list[str],
                             field_name: str) -> str | None:
    """Resolve `struct T` for a field declared as
    `struct T *<field_name>;` anywhere in the given sources.
    Kernel convention frequently names the field after its
    type (atom_context), which makes this reliable for the
    common case."""
    decl_re = re.compile(
        r"struct\s+(\w+)\s*\*\s*" + re.escape(field_name) + r"\s*;")
    for src in source_files:
        m = decl_re.search(src)
        if m:
            return m.group(1)
    return None


def _param_struct_type(source: str, fn_name: str) -> tuple[str | None,
                                                           str | None]:
    """Return (struct_type, param_name) for the first
    pointer-to-struct parameter of fn_name's DEFINITION.

    Anchors on the definition (return-type prefix + `{` body)
    rather than the first `fn_name(...)` occurrence, which may
    be a call site whose argument list contains no type."""
    pat = re.compile(
        r"(?:^|\n)[A-Za-z_][^\n;{}]*?\b" + re.escape(fn_name)
        + r"\s*\((?P<params>[^;{}]*)\)\s*\{")
    m = pat.search(source)
    if not m:
        # Fall back to any occurrence whose params carry a type.
        for mm in re.finditer(
                r"\b" + re.escape(fn_name) + r"\s*\(([^)]*)\)", source):
            if "struct" in mm.group(1):
                m = mm
                params = mm.group(1)
                break
        else:
            return (None, None)
    else:
        params = m.group("params")
    for p in params.split(","):
        pm = re.search(r"struct\s+(\w+)\s*\*\s*(\w+)\s*$", p.strip())
        if pm:
            return (pm.group(1), pm.group(2))
    return (None, None)


def _field_paths_freed(body: str, obj_exprs: list[str]) -> set[str]:
    """Return the set of field paths f such that
    `kfree(<obj>...->f)` or `kfree(<obj>...->a.b)` appears,
    where <obj> is one of the recognised object base
    expressions.  The field path is the suffix after the
    object base.

    Also resolves one level of local aliasing: a destructor
    that does `local = <obj>->f; ...; kfree(local);` frees the
    field through the alias (e.g. maple_release_device does
    `mq = mdev->mq; kfree(mq);`)."""
    freed: set[str] = set()
    # Build alias map: local -> field, for `local = obj->f;`.
    alias: dict[str, str] = {}
    for base in obj_exprs:
        for am in re.finditer(
                r"\b([A-Za-z_]\w*)\s*=\s*" + re.escape(base)
                + r"\s*->\s*([A-Za-z_][\w.]*?)\s*;", body):
            fld = re.sub(r"\[[^\]]*\]", "", am.group(2))
            if "(" not in fld:
                alias[am.group(1)] = fld
    for m in re.finditer(
            r"\b(?:" + _FREE_APIS + r")\s*\(\s*([^;]+?)\s*\)\s*;", body):
        arg = re.sub(r"^\([^)]*\)\s*", "",
                     m.group(1).strip()).strip()
        # Alias free: kfree(local) where local = obj->f.
        if arg in alias:
            freed.add(alias[arg])
            continue
        for base in obj_exprs:
            if arg.startswith(base) and len(arg) > len(base):
                rest = arg[len(base):]
                # rest like "->iio" or "->pmu.attr_groups"
                rest = rest.lstrip()
                if rest.startswith("->"):
                    fld = rest[2:].strip()
                    # Drop trailing array index / subfield deref.
                    fld = re.sub(r"\[[^\]]*\]", "", fld)
                    if fld and "(" not in fld:
                        freed.add(fld)
    return freed


def _local_struct_type(body: str, var: str) -> str | None:
    """Resolve `struct T` for a local variable or parameter
    `var` declared `struct T *var` within the function body or
    signature text supplied as `body`."""
    m = re.search(
        r"struct\s+(\w+)\s*\*\s*" + re.escape(var) + r"\b", body)
    return m.group(1) if m else None


# Constructor name-pairing: map a destructor-name pattern to
# the constructor-name pattern(s) that allocate the object.
_CTOR_PAIRS = [
    (re.compile(r"^(.*)_fini$"), r"\1_init"),
    (re.compile(r"^unregister_(.*)$"), r"register_\1"),
    (re.compile(r"^(.*)_destroy$"), r"\1_create"),
    (re.compile(r"^(.*)_destroy$"), r"\1_init"),
    (re.compile(r"^(.*)_free$"), r"\1_alloc"),
    (re.compile(r"^(.*)_free$"), r"\1_new"),
    (re.compile(r"^(.*)_cleanup$"), r"\1_init"),
    (re.compile(r"^(.*)_cleanup$"), r"\1_setup"),
    (re.compile(r"^(.*)_del$"), r"\1_add"),
    (re.compile(r"^(.*)_remove$"), r"\1_add"),
    (re.compile(r"^(.*)_remove$"), r"\1_probe"),
    (re.compile(r"^(.*)_exit$"), r"\1_init"),
]


def _constructor_names(dtor_name: str) -> list[str]:
    """Return plausible constructor names paired with the
    destructor name by kernel naming convention."""
    out: list[str] = []
    for pat, repl in _CTOR_PAIRS:
        m = pat.match(dtor_name)
        if m:
            out.append(m.expand(repl))
    return out


def _enum_functions(source: str) -> list[tuple[str, str]]:
    """Enumerate (name, body) for top-level function
    definitions in `source` by brace matching.  Coarse but
    sufficient for free-set extraction."""
    out: list[tuple[str, str]] = []
    for m in re.finditer(
            r"(?:^|\n)[A-Za-z_][^\n;{}]*?\b([A-Za-z_]\w*)\s*"
            r"\([^;{}]*\)\s*\{", source):
        name = m.group(1)
        start = m.end()
        depth = 1
        i = start
        n = len(source)
        while i < n and depth > 0:
            c = source[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            i += 1
        if depth == 0:
            out.append((name, source[start:i - 1]))
    return out


def _sibling_destructor_fields(sources: list[str], struct_type: str,
                               exclude_fn: str) -> set[str]:
    """Find OTHER REDUNDANT full destructors of
    `struct struct_type` and collect the field-paths they free.

    A redundant full destructor is a SINGLE function that both
    (a) bare-frees a variable `v` of type T (`kfree(v)`), and
    (b) frees one or more of v's fields (`kfree(v->f)`).  Both
    must occur in the SAME function — this distinguishes a
    redundant alternative teardown (e.g. atom_destroy, which
    frees both ctx and ctx->iio) from a COOPERATING split
    teardown (e.g. configfs drop_item frees only ->name while
    a separate .release frees only the struct).  Cooperating
    callbacks legitimately free disjoint field sets and must
    NOT be treated as evidence of a missing free."""
    fields: set[str] = set()
    for src in sources:
        for fname, fbody in _enum_functions(src):
            if fname == exclude_fn:
                continue
            # Variables of type T in this function.
            t_vars = set(re.findall(
                r"struct\s+" + re.escape(struct_type)
                + r"\s*\*\s*(\w+)", fbody))
            pt, pn = _param_struct_type(src, fname)
            if pt == struct_type and pn:
                t_vars.add(pn)
            for v in t_vars:
                esc = re.escape(v)
                # (a) must bare-free v
                if not re.search(
                        r"\b(?:" + _FREE_APIS + r")\s*\(\s*" + esc
                        + r"\s*\)", fbody):
                    continue
                # (b) collect v->field frees in the SAME function
                for m in re.finditer(
                        r"\b(?:" + _FREE_APIS + r")\s*\(\s*" + esc
                        + r"\s*->\s*([A-Za-z_][\w.]*?)\s*\)", fbody):
                    fields.add(m.group(1))
    return fields


def _constructor_alloc_fields(sources: list[str], struct_type: str,
                              dtor_name: str) -> set[str]:
    """Find the name-paired constructor(s) and collect the
    object fields they allocate (`v->f = <alloc>(...)`)."""
    ctor_names = _constructor_names(dtor_name)
    if not ctor_names:
        return set()
    fields: set[str] = set()
    for src in sources:
        for cname in ctor_names:
            cbody = _function_body(src, cname)
            if cbody is None:
                continue
            # Variables of type T inside the constructor.
            t_vars = set(re.findall(
                r"struct\s+" + re.escape(struct_type)
                + r"\s*\*\s*(\w+)", src))
            # The constructor's primary object may be a
            # parameter of type T; include it.
            pt, pn = _param_struct_type(src, cname)
            if pt == struct_type and pn:
                t_vars.add(pn)
            for v in t_vars:
                esc = re.escape(v)
                for m in re.finditer(
                        esc + r"\s*->\s*([A-Za-z_][\w.]*?)\s*=\s*"
                        r"(?:" + _ALLOC_APIS + r")\s*\(", cbody):
                    fld = re.sub(r"\[[^\]]*\]", "", m.group(1))
                    fields.add(fld)
    return fields

def _transitive_freed_fields(sources: list[str], body: str,
                             obj: str, struct_type: str) -> set[str]:
    """One-level transitive free detection.  For each helper
    called in `body` with the destructor object (or its base)
    as an argument, return the fields that helper frees on a
    variable of `struct_type`.  This avoids false positives
    when a destructor delegates field teardown to a helper
    (e.g. `cleanup_queues(nullb)` frees `nullb->queues`)."""
    base = re.match(r"[A-Za-z_]\w*", obj)
    base_id = base.group(0) if base else obj
    fields: set[str] = set()
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(([^;]*?)\)\s*;", body):
        callee, args = m.group(1), m.group(2)
        if callee in ("kfree", "kvfree", "kfree_skb", "if", "for",
                      "while", "switch", "return", "sizeof"):
            continue
        if not re.search(r"\b" + re.escape(base_id) + r"\b", args):
            continue
        for src in sources:
            cbody = _function_body(src, callee)
            if cbody is None:
                continue
            pt, pn = _param_struct_type(src, callee)
            t_vars = set(re.findall(
                r"struct\s+" + re.escape(struct_type)
                + r"\s*\*\s*(\w+)", src))
            if pt == struct_type and pn:
                t_vars.add(pn)
            for v in t_vars:
                esc = re.escape(v)
                for fm in re.finditer(
                        r"\b(?:" + _FREE_APIS + r")\s*\(\s*" + esc
                        + r"\s*->\s*([A-Za-z_][\w.]*?)\s*\)", cbody):
                    fields.add(fm.group(1))
            break
    return fields



def _sibling_sources(target_file: str) -> list[str]:
    """Return the text of the target file plus sibling .c/.h
    files in the same directory (bounded to keep the scan
    fast)."""
    p = Path(target_file)
    texts: list[str] = []
    try:
        texts.append(p.read_text(errors="replace"))
    except OSError:
        return texts
    d = p.parent
    try:
        headers = sorted(d.glob("*.h"))
        c_files = sorted(d.glob("*.c"))
    except OSError:
        headers, c_files = [], []
    # Headers hold struct declarations (needed for type
    # resolution) and are usually few — scan all of them.
    # Cap the .c files (owned-field evidence) to bound runtime.
    for s in headers[:80] + c_files[:80]:
        if s == p:
            continue
        try:
            texts.append(s.read_text(errors="replace"))
        except OSError:
            pass
    return texts


def analyze(target_file: str, fn_name: str,
            sources: list[str] | None = None) -> DtorVerdict:
    """Analyse (target_file, fn_name) for the incomplete-
    destructor bug class.

    `sources` may be supplied to avoid re-reading the sibling
    files on every call (used by the batch scanner, which
    caches sources per directory).  When None, the sibling
    sources are loaded for `target_file`.  The primary file's
    text must be `sources[0]`."""
    if sources is None:
        sources = _sibling_sources(target_file)
    if not sources:
        return DtorVerdict(False, None, None,
                           reason="source unreadable")
    primary = sources[0]
    body = _function_body(primary, fn_name)
    if body is None:
        return DtorVerdict(False, None, None,
                           reason="function not found")

    bare = _bare_kfree_objects(body)
    # Object exprs that look like a struct object (bare
    # identifier or arrow/dot chain).
    obj_candidates = [b for b in bare
                      if re.fullmatch(r"[A-Za-z_][\w]*"
                                      r"(?:(?:->|\.)[A-Za-z_]\w*)*", b)]
    if not obj_candidates:
        return DtorVerdict(False, None, None,
                           reason="no struct object freed (not a "
                                  "destructor)")
    # The primary object O is the freed expression that is a
    # PREFIX of another freed expression — i.e. both `kfree(O)`
    # and `kfree(O->field)` appear, so O is the container being
    # torn down (e.g. atom_context, freed alongside its
    # ->scratch field).  If several qualify, pick the longest
    # (most specific container).  If none is a prefix of
    # another (e.g. a destructor that frees only `kfree(obj)`
    # with no field-frees in its own body), fall back to the
    # longest bare-freed object.
    prefixes = []
    for o in obj_candidates:
        for other in obj_candidates:
            if other != o and other.startswith(o + "->"):
                prefixes.append(o)
                break
    if prefixes:
        obj = max(prefixes, key=len)
    else:
        obj = max(obj_candidates, key=len)

    # Resolve struct type of the freed object.
    term = _terminal_field(obj)
    if term is not None:
        struct_type = _struct_type_of_terminal(sources, term)
    else:
        # obj is a bare param/local — resolve from its
        # declaration in the function body or the signature.
        sig_and_body = (primary[max(0, primary.find(fn_name) - 200):
                                primary.find(fn_name) + 200]
                        + body)
        struct_type = _local_struct_type(sig_and_body, obj)
        if struct_type is None:
            # Only inherit a parameter's struct type when the
            # freed object IS that parameter.  Otherwise a
            # destructor that frees a void* arg (e.g.
            # ldc_free_exp_dring frees `buf`) would wrongly
            # inherit the type of an unrelated struct param
            # (`lp`), pulling in that struct's owned fields.
            pt, pn = _param_struct_type(primary, fn_name)
            if pn == obj:
                struct_type = pt
    if struct_type is None:
        return DtorVerdict(False, None, obj,
                           reason=f"could not resolve struct type "
                                  f"of `{obj}`")

    freed = _field_paths_freed(body, [obj])
    # Transitive frees: a real destructor often delegates
    # field teardown to a helper (e.g. null_del_dev calls
    # cleanup_queues(nullb) which frees nullb->queues).  For
    # each helper called in D's body with the object (or a
    # base of it) as an argument, add the fields that helper
    # frees on a variable of the same struct type.
    freed |= _transitive_freed_fields(sources, body, obj,
                                      struct_type)
    # Tight owned-field signal: a field is "expected to be
    # freed here" only when a SIBLING full destructor of the
    # same struct type frees it, OR the name-paired
    # constructor allocates it.  This is far more precise than
    # "any function touches the field" — it requires evidence
    # that the field participates in the SAME object lifecycle.
    sibling = _sibling_destructor_fields(sources, struct_type, fn_name)
    ctor = _constructor_alloc_fields(sources, struct_type, fn_name)
    owned = (sibling | ctor)
    missing = owned - freed
    if not missing:
        return DtorVerdict(
            True, struct_type, obj, freed, owned, set(),
            reason=f"destructor of `struct {struct_type}` frees all "
                   f"{len(owned)} cross-function owned field(s); "
                   f"complete")
    return DtorVerdict(
        True, struct_type, obj, freed, owned, missing,
        reason=f"destructor of `struct {struct_type}` frees "
               f"{sorted(freed)}; sibling-destructor/constructor "
               f"evidence shows owned fields {sorted(owned)}; "
               f"MISSING {sorted(missing)}")


if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        print("usage: destructor_completeness.py <file.c> <function>",
              file=sys.stderr)
        sys.exit(2)
    v = analyze(sys.argv[1], sys.argv[2])
    print(f"is_destructor : {v.is_destructor}")
    print(f"struct_type   : {v.struct_type}")
    print(f"object_expr   : {v.object_expr}")
    print(f"freed_fields  : {sorted(v.freed_fields)}")
    print(f"owned_fields  : {sorted(v.owned_fields)}")
    print(f"missing_fields: {sorted(v.missing_fields)}")
    print(f"reason        : {v.reason}")
    sys.exit(0 if v.missing_fields else 1)
