#!/usr/bin/env python3
"""
instrument.py — generalised per-file instrumentation tool
for the synthetic-checkpoint property modules.

Reads a kernel .c source file and rewrites it to insert
`__assert_<property>(...)` checkpoints at sites identified
by per-shape regex pre-filters.  The instrumented source
can then be compiled and verified by CBMC; precondition
failures on the inserted checkpoints surface as 'real
candidate' verdicts in the existing pipeline.

Bug shapes supported in v1 (covers 5 of the 12 synthetic-
checkpoint modules):

* null_after_alloc            (kmalloc-then-deref)
* resource_leak_on_error_path (alloc-then-early-return)
* use_after_free_generic      (kfree-then-deref)
* integer_overflow_in_alloc_size (kmalloc-with-multiplication)
* copy_from_user_size_check   (copy_from_user with variable len)

Limitations (v1):
* Regex-based; misses compound LHS like `mk->mp = kmalloc(...)`.
  Productising would use Coccinelle's structural matching.
* Single-statement-context only — doesn't follow control flow
  through if/else branches.
* No de-duplication when the same bug class is checkable at
  multiple sites in the same function.

Usage:
  ./instrument.py kernel.c -o instrumented.c [--shapes SHAPES]
  ./instrument.py --list-shapes
"""
import argparse
import re
import sys
from pathlib import Path


# ---------------------------------------------------------------------
# Shape registry: each shape has (pattern-list, instrumentation-fn).
# Each shape is independent; a single source file may produce checkpoints
# for many shapes simultaneously.
# ---------------------------------------------------------------------

SHAPES = {}


def shape(name):
    def deco(fn):
        SHAPES[name] = fn
        return fn
    return deco


def find_function_bounds(src: str, line_num: int) -> tuple[int, int] | None:
    """Find the start/end line index of the function containing
    the given 0-indexed `line_num`."""
    lines = src.splitlines()
    start = line_num
    while start > 0 and lines[start].strip() != "{":
        start -= 1
    if start <= 0:
        return None
    depth = 1
    end = start + 1
    while end < len(lines) and depth > 0:
        depth += lines[end].count("{") - lines[end].count("}")
        end += 1
    return (start + 1, end)


def has_null_check_between(lines: list[str], var: str, lo: int, hi: int) -> bool:
    pat = re.compile(
        rf"\bif\s*\(\s*!\s*{re.escape(var)}\b|"
        rf"\bif\s*\(\s*{re.escape(var)}\s*==\s*NULL\b|"
        rf"\bif\s*\(\s*IS_ERR\(\s*{re.escape(var)}\b|"
        rf"\bif\s*\(\s*IS_ERR_OR_NULL\(\s*{re.escape(var)}\b"
    )
    for i in range(lo, hi):
        if i < len(lines) and pat.search(lines[i]):
            return True
    return False


def has_kfree_between(lines: list[str], var: str, lo: int, hi: int) -> bool:
    pat = re.compile(rf"\bk(?:v)?free\(\s*{re.escape(var)}\b")
    for i in range(lo, hi):
        if i < len(lines) and pat.search(lines[i]):
            return True
    return False


def has_reassign_between(lines: list[str], var: str, lo: int, hi: int) -> bool:
    pat = re.compile(rf"\b{re.escape(var)}\s*=")
    for i in range(lo, hi):
        if i < len(lines) and pat.search(lines[i]):
            return True
    return False


# ---------------------------------------------------------------------
# Shape: null_after_alloc
# Pattern: x = kmalloc(...) followed by x->Y without intervening NULL check.
# ---------------------------------------------------------------------

ALLOC_RE = re.compile(
    r"(?P<lhs>\w+)\s*=\s*"
    r"(?P<api>kmalloc|kzalloc|kcalloc|alloc_skb|"
    r"kmem_cache_alloc|vmalloc|kvmalloc|alloc_skb_fclone|"
    r"alloc_pages|alloc_skb_with_frags)"
    r"\s*\([^;]*\)\s*;"
)


@shape("null_after_alloc")
def instrument_null_after_alloc(lines: list[str]) -> list[tuple[int, str]]:
    """Returns list of (line_index_to_insert_at, code) tuples."""
    src = "".join(lines)
    inserts = []
    for line_idx, line in enumerate(lines):
        m = ALLOC_RE.search(line)
        if not m:
            continue
        var = m.group("lhs")
        api = m.group("api")
        bounds = find_function_bounds(src, line_idx)
        if bounds is None:
            continue
        fn_start, fn_end = bounds
        deref_re = re.compile(rf"\b{re.escape(var)}\s*->")
        for j in range(line_idx + 1, min(fn_end, len(lines))):
            if not deref_re.search(lines[j]):
                continue
            if has_null_check_between(lines, var, line_idx + 1, j):
                continue
            indent = re.match(r"^(\s*)", lines[j]).group(1)
            inserts.append((j, (
                f"{indent}__assert_safe_to_deref({var});"
                f"  /* AUTO null_after_alloc / {api} */\n")))
            break
    return inserts


# ---------------------------------------------------------------------
# Shape: resource_leak_on_error_path
# Pattern: x = kmalloc(...) followed by `return ...` without intervening
# kfree(x) or reassignment.
# ---------------------------------------------------------------------

@shape("resource_leak_on_error_path")
def instrument_resource_leak(lines: list[str]) -> list[tuple[int, str]]:
    src = "".join(lines)
    inserts = []
    for line_idx, line in enumerate(lines):
        m = ALLOC_RE.search(line)
        if not m:
            continue
        var = m.group("lhs")
        api = m.group("api")
        bounds = find_function_bounds(src, line_idx)
        if bounds is None:
            continue
        fn_start, fn_end = bounds
        # Look for `return ...;` lines.  Skip if there's an
        # intervening kfree(x) or x = ... reassignment.
        for j in range(line_idx + 1, min(fn_end, len(lines))):
            if not re.search(r"^\s*return\b", lines[j]):
                continue
            if has_kfree_between(lines, var, line_idx + 1, j):
                continue
            if has_reassign_between(lines, var, line_idx + 1, j):
                continue
            # Track this allocation when we see it; check at the
            # return.  Insert two checkpoints: `leak_alloc_track(var)`
            # right after the alloc (idempotent), and
            # `__assert_no_leak_at_exit(var)` right before the return.
            indent = re.match(r"^(\s*)", lines[j]).group(1)
            inserts.append((j, (
                f"{indent}__assert_no_leak_at_exit({var});"
                f"  /* AUTO resource_leak / {api} */\n")))
            break
    return inserts


# ---------------------------------------------------------------------
# Shape: use_after_free_generic
# Pattern: kfree(x) followed by x->Y (no intervening reassignment).
# ---------------------------------------------------------------------

KFREE_RE = re.compile(r"\bk(?:v)?free\(\s*(?P<arg>\w+)\s*\)\s*;")


@shape("use_after_free_generic")
def instrument_use_after_free(lines: list[str]) -> list[tuple[int, str]]:
    src = "".join(lines)
    inserts = []
    for line_idx, line in enumerate(lines):
        m = KFREE_RE.search(line)
        if not m:
            continue
        var = m.group("arg")
        bounds = find_function_bounds(src, line_idx)
        if bounds is None:
            continue
        fn_start, fn_end = bounds
        deref_re = re.compile(rf"\b{re.escape(var)}\s*->")
        for j in range(line_idx + 1, min(fn_end, len(lines))):
            if not deref_re.search(lines[j]):
                continue
            if has_reassign_between(lines, var, line_idx + 1, j):
                continue
            indent = re.match(r"^(\s*)", lines[j]).group(1)
            inserts.append((j, (
                f"{indent}__assert_not_freed({var});"
                f"  /* AUTO use_after_free_generic */\n")))
            break
    return inserts


# ---------------------------------------------------------------------
# Shape: integer_overflow_in_alloc_size
# Pattern: kmalloc(N * sizeof(...)).  Checkpoint: assert size_safe.
# ---------------------------------------------------------------------

ALLOC_MUL_RE = re.compile(
    r"(?P<api>kmalloc|kzalloc)\s*\("
    r"\s*(?P<n>\w+)\s*\*\s*sizeof\s*\([^)]+\)\s*,"
)


@shape("integer_overflow_in_alloc_size")
def instrument_integer_overflow(lines: list[str]) -> list[tuple[int, str]]:
    inserts = []
    for line_idx, line in enumerate(lines):
        m = ALLOC_MUL_RE.search(line)
        if not m:
            continue
        n = m.group("n")
        api = m.group("api")
        # Skip if `n` is a constant literal (overflow not possible).
        if re.fullmatch(r"\d+|0x[0-9a-fA-F]+", n):
            continue
        indent = re.match(r"^(\s*)", line).group(1)
        inserts.append((line_idx, (
            f"{indent}__assert_size_safe({n}, sizeof(*{n}));"
            f"  /* AUTO integer_overflow / {api} */\n")))
    return inserts


# ---------------------------------------------------------------------
# Shape: copy_from_user_size_check
# Pattern: copy_from_user(dst, src, len) where len is variable.
# Checkpoint: __assert_copy_safe(sizeof(dst-equivalent), len).
# ---------------------------------------------------------------------

CFU_RE = re.compile(
    r"copy_from_user\s*\(\s*"
    r"(?P<dst>[^,]+?)\s*,\s*"
    r"(?P<src>[^,]+?)\s*,\s*"
    r"(?P<len>[^,)]+?)\s*\)"
)


@shape("copy_from_user_size_check")
def instrument_copy_from_user(lines: list[str]) -> list[tuple[int, str]]:
    inserts = []
    for line_idx, line in enumerate(lines):
        m = CFU_RE.search(line)
        if not m:
            continue
        dst = m.group("dst").strip()
        length = m.group("len").strip()
        # Skip if len is a constant.
        if re.fullmatch(r"\d+|0x[0-9a-fA-F]+|sizeof\s*\([^)]+\)", length):
            continue
        # Use sizeof(*<dst>) as a heuristic for capacity.  Many cases
        # this is a struct copy.
        indent = re.match(r"^(\s*)", line).group(1)
        inserts.append((line_idx, (
            f"{indent}__assert_copy_safe(sizeof({dst}[0]), {length});"
            f"  /* AUTO copy_from_user_size_check */\n")))
    return inserts


# ---------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------

# Header to prepend so the instrumented source compiles standalone.
HEADER = """\
/* AUTO-INSERTED by integration/linux/scan/tools/instrument.py */
extern void __assert_safe_to_deref(const void *p);
extern void __assert_no_leak_at_exit(const void *p);
extern void __assert_not_freed(const void *p);
extern void __assert_size_safe(unsigned long n, unsigned long elem_size);
extern void __assert_copy_safe(unsigned long dst_capacity, unsigned long len);

"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "source", type=Path, nargs="?",
        help="Kernel .c file to instrument.")
    parser.add_argument(
        "--output", "-o", type=Path,
        help="Write instrumented source here (default: stdout).")
    parser.add_argument(
        "--shapes",
        default=",".join(SHAPES),
        help="Comma-separated list of shapes to apply.  Default: all.")
    parser.add_argument(
        "--no-header", action="store_true",
        help="Skip the auto-inserted extern declarations.")
    parser.add_argument(
        "--list-shapes", action="store_true",
        help="List available shapes and exit.")
    args = parser.parse_args()

    if args.list_shapes:
        for s in SHAPES:
            print(s)
        return 0

    if not args.source:
        parser.error("source file required (or use --list-shapes)")

    src = args.source.read_text()
    lines = src.splitlines(keepends=True)

    requested = args.shapes.split(",")
    all_inserts = []
    per_shape_count = {}
    for s in requested:
        if s not in SHAPES:
            print(f"unknown shape: {s}", file=sys.stderr)
            print(f"available: {', '.join(SHAPES)}", file=sys.stderr)
            return 2
        ins = SHAPES[s](lines)
        all_inserts.extend(ins)
        per_shape_count[s] = len(ins)

    # Apply inserts in reverse order so indices stay valid.
    all_inserts.sort(key=lambda x: -x[0])
    for idx, code in all_inserts:
        lines.insert(idx, code)

    out = "".join(lines)
    if not args.no_header:
        out = HEADER + out

    if args.output:
        args.output.write_text(out)
    else:
        sys.stdout.write(out)

    print(f"Instrumented {sum(per_shape_count.values())} site(s):",
          file=sys.stderr)
    for s, c in per_shape_count.items():
        if c > 0:
            print(f"  {s}: {c}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
