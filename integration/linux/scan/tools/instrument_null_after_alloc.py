#!/usr/bin/env python3
"""
instrument_null_after_alloc.py — prototype per-file
instrumentation for the null_after_alloc module.

Reads a kernel .c source file, finds allocator-then-deref
patterns matching null_after_alloc.cocci, and rewrites the
file to insert `__assert_safe_to_deref(p);` calls between
the allocation and each subsequent dereference.

The instrumented file is written to a temp path and printed
to stdout (or written via --output).

This is a PROTOTYPE — the intent is to demonstrate that
cocci-driven instrumentation can lift the
synthetic-checkpoint modules from "cocci-only" to "fully
per-file CBMC-verified".  The current v1 prototype handles
just one bug shape (kmalloc + immediate deref).

Usage:
  ./instrument_null_after_alloc.py kernel.c > kernel_inst.c
"""
import argparse
import re
import sys
from pathlib import Path


# Regex pattern for "x = kmalloc/kzalloc/kcalloc/alloc_skb(...)"
# followed by "x->" within the same function body.
ALLOC_RE = re.compile(
    r"(?P<lhs>\w+)\s*=\s*"
    r"(?P<api>kmalloc|kzalloc|kcalloc|alloc_skb|"
    r"kmem_cache_alloc|vmalloc|kvmalloc)"
    r"\s*\([^;]*\)\s*;"
)


def find_function_bounds(src: str, line_num: int) -> tuple[int, int] | None:
    """Find the start/end line of the function containing `line_num`.
    Heuristic: walk backwards for the opening `{` after a function
    signature, then forward to its matching `}`."""
    lines = src.splitlines()
    # Look back for the function signature.
    start = line_num - 1
    while start > 0 and lines[start].strip() != "{":
        start -= 1
    if start <= 0:
        return None
    # Now walk forward counting braces.
    depth = 1
    end = start + 1
    while end < len(lines) and depth > 0:
        depth += lines[end].count("{") - lines[end].count("}")
        end += 1
    return (start + 1, end)


def is_null_checked(src_lines: list[str], var: str,
                    alloc_line: int, deref_line: int) -> bool:
    """Heuristic: between alloc_line and deref_line, is there a
    NULL check on `var`?"""
    null_check = re.compile(
        rf"\bif\s*\(\s*!\s*{re.escape(var)}\b|"
        rf"\bif\s*\(\s*{re.escape(var)}\s*==\s*NULL\b|"
        rf"\bif\s*\(\s*IS_ERR\(\s*{re.escape(var)}\b|"
        rf"\bif\s*\(\s*IS_ERR_OR_NULL\(\s*{re.escape(var)}\b"
    )
    for i in range(alloc_line, deref_line):
        if i < len(src_lines) and null_check.search(src_lines[i]):
            return True
    return False


def instrument(src: str) -> tuple[str, int]:
    """Find allocator-then-deref patterns and insert
    __assert_safe_to_deref calls.  Returns (modified_src,
    n_inserts)."""
    lines = src.splitlines(keepends=True)
    inserts: list[tuple[int, str]] = []  # (line_index, code)
    n_inserts = 0

    for line_idx, line in enumerate(lines):
        m = ALLOC_RE.search(line)
        if not m:
            continue
        var = m.group("lhs")
        api = m.group("api")

        # Find subsequent dereferences of `var` within the function.
        bounds = find_function_bounds(src, line_idx + 1)
        if bounds is None:
            continue
        fn_start, fn_end = bounds

        # Look for `var->` from line_idx+1 to fn_end.
        deref_re = re.compile(
            rf"\b{re.escape(var)}\s*->")
        for j in range(line_idx + 1, fn_end):
            if j >= len(lines):
                break
            deref_match = deref_re.search(lines[j])
            if not deref_match:
                continue
            # Found a deref.  Is there a null check between?
            if is_null_checked(lines, var, line_idx + 1, j):
                continue
            # Vulnerable shape!  Insert the checkpoint just
            # before the deref line.
            indent = re.match(r"^(\s*)", lines[j]).group(1)
            checkpoint = (
                f"{indent}__assert_safe_to_deref({var});"
                f"  /* AUTO-INSERTED null_after_alloc / {api} */\n"
            )
            inserts.append((j, checkpoint))
            n_inserts += 1
            # Only emit one checkpoint per (var, deref) pair.
            break

    # Apply inserts in reverse order so indices stay valid.
    inserts.sort(key=lambda x: -x[0])
    for idx, code in inserts:
        lines.insert(idx, code)

    return "".join(lines), n_inserts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path,
                        help="Kernel .c file to instrument.")
    parser.add_argument("--output", "-o", type=Path,
                        help="Write instrumented source here "
                             "(default: stdout).")
    parser.add_argument("--header", action="store_true",
                        help="Prepend the __assert_safe_to_deref "
                             "declaration (for stand-alone "
                             "instrumented compilation).")
    args = parser.parse_args()

    src = args.source.read_text()
    instrumented, n = instrument(src)

    if args.header:
        instrumented = (
            "/* AUTO-INSERTED by instrument_null_after_alloc.py */\n"
            "extern void __assert_safe_to_deref(const void *p);\n"
            "\n"
        ) + instrumented

    if args.output:
        args.output.write_text(instrumented)
        print(f"Inserted {n} checkpoint(s); wrote {args.output}",
              file=sys.stderr)
    else:
        sys.stdout.write(instrumented)
        print(f"Inserted {n} checkpoint(s)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
