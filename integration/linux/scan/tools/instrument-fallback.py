#!/usr/bin/env python3
"""instrument-fallback.py — per-return manual instrumentation
for many-returns functions where cocci's CFG analysis aborts.

When `instrument-cocci.sh` reports zero insertions for a target
shape on a target function, this script provides a coarser
fallback: it brace-tracks the target function's body, locates
every alloc-API call site, and inserts:

  * `leak_alloc_track(<lhs>);` after each alloc assignment.
  * `__assert_no_leak_at_exit(<lhs>);` before each `return`
    statement reachable within that function (where <lhs> is
    in scope — i.e. declared at function scope or as a
    parameter).

This is sound but coarser than cocci: returns where the alloc
provably hasn't run trivially satisfy the assert (the ghost
flag is 0), so over-asserting at every return doesn't yield
false positives.

Usage:
  instrument-fallback.py <input.c> <output.c>
      --function FUNC --shape SHAPE [--shape SHAPE ...]

Supported shapes:
  resource_leak_on_error_path  — alloc-API tracking
  use_after_free_generic       — kfree-then-deref tracking
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable

# Allocator-API regex per shape.  Mirrors what's in
# integration/linux/scan/tools/instrument-cocci/*.cocci
#
# Note: devm_* allocators are deliberately EXCLUDED from
# resource-leak tracking.  These are devres-managed
# (devm_kmalloc, devm_kzalloc, etc.) and the underlying
# memory is auto-freed when the parent device is unbound.
# Treating them as ordinary allocations produces false
# positives because callers reasonably never invoke
# devm_kfree explicitly.
_ALLOC_APIS = (
    r"k(?:malloc|zalloc|calloc|malloc_array|memdup|strdup|"
    r"asprintf)|"
    r"kv(?:malloc|zalloc|malloc_array|calloc)|"
    r"v(?:malloc|zalloc)|"
    r"alloc_skb|"
    r"kmem_cache_(?:alloc|zalloc)|"
    r"alloc_workqueue"
)

# Free-API regex for use_after_free.
_FREE_APIS = (
    r"k(?:free|vfree|free_skb)|devm_kfree"
)

# Match an alloc assignment: "lhs = api(...)" with optional
# trailing args/casts and a semicolon.  We don't try to handle
# multi-line statements perfectly — the kernel is mostly
# one-statement-per-line.
_ALLOC_ASSIGN_RE = re.compile(
    r"^(?P<indent>\s*)"
    # Optional declaration prefix: type words possibly with
    # storage-class qualifiers, ending in a star and/or
    # whitespace before the LHS.  The previous form
    # `[\w\s\*]+?\s+` failed to match `struct foo *x =` because
    # `\s+` required a space between the prefix and the LHS,
    # but kernel style writes `*x` with no space after the
    # star.  This form ends the prefix on either whitespace OR
    # one or more stars.
    r"(?:[\w\s]+?[\s\*]\s*\**\s*)?"
    r"(?P<lhs>[A-Za-z_][\w\.\->]*)\s*=\s*"
    r"(?:\([\w\s\*]*\)\s*)?"  # optional cast
    r"(?P<api>" + _ALLOC_APIS + r")\s*\("
)

_FREE_CALL_RE = re.compile(
    r"^(?P<indent>\s*)"
    r"(?:" + _FREE_APIS + r")\s*\(\s*(?P<arg>[\w\.\->]+)\s*[,)]"
)

# Match `*OUT = LHS;` patterns that indicate ownership transfer
# of a tracked allocation through an out-pointer parameter.
# Common kernel idiom for constructor-style allocation
# functions (e.g. `int snd_midi_event_new(int bufsize,
# struct snd_midi_event **rdev) { ... *rdev = dev; return 0; }`).
_OWNERSHIP_TRANSFER_RE = re.compile(
    r"^(?P<indent>\s*)"
    r"\*\s*(?P<out>[\w]+)\s*=\s*(?P<lhs>[\w\.\->]+)\s*;"
)

_RETURN_RE = re.compile(r"^(?P<indent>\s*)return\b")


def _find_function_body(source: str, fn_name: str
                        ) -> tuple[int, int] | None:
    """Locate the function body's char-offset range.

    Returns (body_start_offset, body_end_offset) where
    body_start is the char AFTER the opening '{' and body_end
    is the char BEFORE the closing '}'.  Returns None if not
    found.
    """
    # Match "<type> <fn_name>(..." with the opening brace on
    # the same line or the next line.  Whole-word match on
    # the function name.
    pattern = re.compile(
        r"(?:^|\n)\s*"
        r"(?:[\w\s\*\(\)]+?\s+)?"  # return type / qualifiers
        + re.escape(fn_name) + r"\s*\("
    )
    for m in pattern.finditer(source):
        # Find the matching ')' for this '('.
        i = m.end() - 1  # the '('
        depth = 0
        while i < len(source):
            if source[i] == "(":
                depth += 1
            elif source[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if i >= len(source):
            continue
        # Now find the body's '{'.  Skip whitespace and
        # __attribute__ noise.
        j = i + 1
        while j < len(source) and source[j] != "{":
            if source[j] == ";":
                # forward-decl, not a definition
                j = -1
                break
            j += 1
        if j < 0 or j >= len(source):
            continue
        # Match braces.
        depth = 1
        k = j + 1
        while k < len(source) and depth > 0:
            if source[k] == "{":
                depth += 1
            elif source[k] == "}":
                depth -= 1
            k += 1
        if depth != 0:
            continue
        return (j + 1, k - 1)
    return None


def _split_lines_with_offsets(text: str
                              ) -> list[tuple[int, int, str]]:
    """Return list of (start_offset, end_offset, line) where
    end_offset is the offset just past the trailing newline."""
    out: list[tuple[int, int, str]] = []
    pos = 0
    for line in text.splitlines(keepends=True):
        out.append((pos, pos + len(line), line))
        pos += len(line)
    return out


def _function_scope_decls(body: str) -> set[str]:
    """Heuristically extract names of variables declared at
    function scope (depth-0 inside the body).  Brace-aware."""
    names: set[str] = set()
    depth = 0
    pos = 0
    for ch in body:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        pos += 1
    # Re-scan for declarations, only counting depth-0.
    depth = 0
    pos = 0
    line_start = 0
    line: list[str] = []
    for ch in body:
        line.append(ch)
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        elif ch == ";" and depth == 0:
            stmt = "".join(line).strip().rstrip(";").strip()
            # Pattern: <type-words> <name>[, <name>] [= ...]
            # We just look for "<word> <word>" at start.
            m = re.match(
                r"(?:const\s+|static\s+|volatile\s+|"
                r"unsigned\s+|signed\s+|struct\s+|"
                r"enum\s+|union\s+)*"
                r"[\w]+\s+(\*+\s*)?"
                r"([\w]+)\b",
                stmt)
            if m and not stmt.startswith(("return ", "if ",
                                          "while ", "for ",
                                          "switch ", "do ",
                                          "goto ", "case ",
                                          "default ")):
                names.add(m.group(2))
            line = []
        elif ch == "\n":
            # tolerate embedded newlines in multi-line stmts
            pass
    return names


def _function_parameter_names(source: str, fn_name: str
                              ) -> set[str]:
    """Extract parameter names from the function signature."""
    pattern = re.compile(
        r"(?:^|\n)\s*"
        r"(?:[\w\s\*\(\)]+?\s+)?"
        + re.escape(fn_name) + r"\s*\("
        r"(?P<params>[^)]*)\)"
    )
    m = pattern.search(source)
    if not m:
        return set()
    params = m.group("params")
    names: set[str] = set()
    for p in params.split(","):
        p = p.strip()
        if not p or p == "void":
            continue
        # Last identifier is the name; strip * and [].
        name_match = re.search(r"(\w+)\s*(?:\[\s*\])?\s*$", p)
        if name_match:
            names.add(name_match.group(1))
    return names


def _instrument_resource_leak(source: str, fn_name: str
                              ) -> tuple[str, int]:
    """Apply resource_leak_on_error_path fallback to the named
    function.  Returns (modified_source, num_insertions)."""
    body_range = _find_function_body(source, fn_name)
    if body_range is None:
        return source, 0
    body_start, body_end = body_range

    # In-scope identifiers: function parameters + declarations
    # at body depth 0.
    body_text = source[body_start:body_end]
    in_scope = (_function_parameter_names(source, fn_name)
                | _function_scope_decls(body_text))

    # Find alloc assignments and record tracked LHS names.
    tracked: list[tuple[int, str]] = []  # (offset_after_;, lhs)
    body_lines = _split_lines_with_offsets(body_text)
    for ls, le, line in body_lines:
        m = _ALLOC_ASSIGN_RE.match(line)
        if not m:
            continue
        lhs = m.group("lhs")
        # Find the ';' terminating this statement, scanning
        # from this line's start.  May span lines for long
        # arg lists; track paren depth so a ';' inside the
        # call's arguments isn't taken.
        j = ls
        depth = 0
        while j < len(body_text):
            c = body_text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == ";" and depth == 0:
                tracked.append((j + 1, lhs))
                break
            j += 1
    if not tracked:
        return source, 0

    # Find return statements.  For each, find the terminating
    # semicolon so we can REPLACE the entire `return EXPR;`
    # statement with a `{ asserts; return EXPR; }` block — this
    # is sound regardless of whether the return was the brace-
    # less body of an outer if/else/while.
    return_ranges: list[tuple[int, int]] = []  # (start, end_excl)
    for ls, le, line in body_lines:
        m = _RETURN_RE.match(line)
        if not m:
            continue
        # Find the ';' terminating this return.  Track depth
        # for parenthesised initializer-list returns.
        j = ls
        depth = 0
        rstart = ls + len(m.group("indent"))
        while j < len(body_text):
            c = body_text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == ";" and depth == 0:
                return_ranges.append((rstart, j + 1))
                break
            j += 1

    # Build the modification list.  Three kinds of edits:
    #   * insertion at offset (alloc tracker after ';')
    #   * insertion at offset (free tracker after ';')
    #   * replacement of [start,end) with new text (return)
    # All encoded as (start, end, text) with end==start for
    # pure insertions.
    edits: list[tuple[int, int, str]] = []
    for off, lhs in tracked:
        edits.append((off, off, f"\n\tleak_alloc_track({lhs});"))

    # Find kfree/kvfree/etc. calls within the function body
    # and insert leak_alloc_freed(arg) after each so the
    # ghost flag clears when the kernel actually frees the
    # pointer.  Without this, the per-return assertion
    # always fires on functions that free at a common
    # error-cleanup label (e.g. `out: kfree(p); return ret;`).
    for ls, le, line in body_lines:
        fm = _FREE_CALL_RE.match(line)
        if not fm:
            continue
        arg = fm.group("arg")
        # Scan to terminating ';'.
        j = ls
        depth = 0
        while j < len(body_text):
            c = body_text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == ";" and depth == 0:
                edits.append((
                    j + 1, j + 1,
                    f"\n{fm.group('indent')}"
                    f"leak_alloc_freed({arg});"
                ))
                break
            j += 1

    # Find `*OUT = LHS;` ownership-transfer patterns.  When a
    # tracked allocation is stored into an out-pointer
    # parameter (the `*rdev = dev;` constructor-style idiom),
    # the function is transferring ownership to the caller —
    # treat that as freed-from-this-function's-perspective
    # and clear the ghost flag.  Without this, constructor-
    # style allocators are flagged as leaks on success
    # paths.
    param_names = _function_parameter_names(source, fn_name)
    tracked_lhs_set = {lhs for _, lhs in tracked}
    for ls, le, line in body_lines:
        om = _OWNERSHIP_TRANSFER_RE.match(line)
        if not om:
            continue
        out_name = om.group("out")
        lhs = om.group("lhs")
        if out_name not in param_names:
            continue
        # Only clear if the assigned LHS is itself a tracked
        # allocation.  Skip `*rdev = NULL;` and similar.
        if lhs not in tracked_lhs_set:
            continue
        # Compute the set of tracked variables to clear:
        # the LHS itself plus any tracked variable that
        # is reachable through the LHS.  Common idiom:
        #   dev = kzalloc(...);
        #   dev->buf = kmalloc(...);
        #   *rdev = dev;        // transfers `dev` AND sub-allocs.
        # We treat any tracked var whose name starts with
        # `<lhs>->` or `<lhs>.` as a sub-allocation that
        # transitively follows ownership.
        to_clear = [lhs]
        for other in tracked_lhs_set:
            if other == lhs:
                continue
            if (other.startswith(f"{lhs}->")
                    or other.startswith(f"{lhs}.")):
                to_clear.append(other)
        # Scan to terminating ';'.
        j = ls
        depth = 0
        while j < len(body_text):
            c = body_text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == ";" and depth == 0:
                indent = om.group('indent')
                clear_block = "\n" + "".join(
                    f"{indent}leak_alloc_freed({v});"
                    for v in to_clear)
                edits.append((j + 1, j + 1, clear_block))
                break
            j += 1
    # Tracked LHS names (deduplicated, ordered by appearance).
    seen: set[str] = set()
    tracked_lhs: list[str] = []
    for _, lhs in tracked:
        if lhs not in seen:
            seen.add(lhs)
            tracked_lhs.append(lhs)

    # Per-return assertion strategy.  We use a single
    # __assert_no_outstanding_leak() check per return rather
    # than one __assert_no_leak_at_exit(<lhs>) per (return ×
    # tracked variable).  The aggregated version checks the
    # global ghost table for ANY outstanding allocation,
    # reducing assertion fan-out from O(returns × vars) to
    # O(returns).  Critical for many-allocations functions
    # where the fan-out variant blew CBMC's symex budget.
    for rstart, rend in return_ranges:
        return_stmt = body_text[rstart:rend]
        replacement = ("{ __assert_no_outstanding_leak(); "
                       + return_stmt + " }")
        edits.append((rstart, rend, replacement))

    # Apply edits in reverse offset order.  Sort by start
    # offset descending; for tied starts, replacements (end>start)
    # before pure inserts (end==start).
    edits.sort(key=lambda x: (-x[0], -x[1]))
    new_body = body_text
    for start, end, text in edits:
        new_body = new_body[:start] + text + new_body[end:]

    new_source = (source[:body_start] + new_body
                  + source[body_end:])
    return new_source, len(edits)


def _instrument_use_after_free(source: str, fn_name: str
                               ) -> tuple[str, int]:
    """Apply use_after_free_generic fallback: insert
    `__assert_not_freed(p);` BEFORE every kfree(p) call.  This
    is unsound for use-after-free detection (we can't know
    without semantic analysis), but it does insert an ASSERT
    at the point of free which CBMC can use to flag double-
    frees and demonstrate the bug class on simple cases."""
    body_range = _find_function_body(source, fn_name)
    if body_range is None:
        return source, 0
    body_start, body_end = body_range
    body_text = source[body_start:body_end]
    body_lines = _split_lines_with_offsets(body_text)

    inserts: list[tuple[int, int, str]] = []
    for ls, le, line in body_lines:
        m = _FREE_CALL_RE.match(line)
        if not m:
            continue
        arg = m.group("arg")
        # Find the ';' terminating this kfree call.
        j = ls
        depth = 0
        cstart = ls + len(m.group("indent"))
        while j < len(body_text):
            c = body_text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == ";" and depth == 0:
                # Replace `kfree(p);` with
                # `{ __assert_not_freed(p); kfree(p); }` so the
                # assert AND the free are bracketed when this
                # was the brace-less body of an outer if.
                stmt = body_text[cstart:j + 1]
                inserts.append((
                    cstart, j + 1,
                    "{ __assert_not_freed(" + arg + "); "
                    + stmt + " }"))
                break
            j += 1
    inserts.sort(key=lambda x: (-x[0], -x[1]))
    new_body = body_text
    for start, end, text in inserts:
        new_body = new_body[:start] + text + new_body[end:]
    return (source[:body_start] + new_body + source[body_end:],
            len(inserts))


_SHAPE_HANDLERS = {
    "resource_leak_on_error_path": _instrument_resource_leak,
    "use_after_free_generic": _instrument_use_after_free,
    "null_after_alloc": None,  # filled below
    "cancel_work_before_free": None,  # filled below
}


# APIs whose return value can legitimately be NULL.  Wider
# than the leak-tracking _ALLOC_APIS because we want to catch
# the "kernel get/lookup returned NULL" pattern, not just
# memory-alloc.  This list is conservative — adding too
# many APIs increases the FP rate.
_NULLABLE_RETURN_APIS = (
    r"k(?:malloc|zalloc|calloc|malloc_array|memdup|strdup|"
    r"asprintf)|"
    r"kv(?:malloc|zalloc|malloc_array|calloc)|"
    r"v(?:malloc|zalloc)|"
    r"alloc_skb|"
    r"kmem_cache_(?:alloc|zalloc)|"
    r"alloc_workqueue|"
    # ACPI device lookup APIs that can return NULL.
    r"acpi_get_first_physical_node|"
    r"acpi_dev_get_first_match_dev|"
    r"acpi_match_device|"
    # Generic kernel get/lookup that frequently returns NULL.
    r"of_find_node_by_name|"
    r"of_get_child_by_name|"
    r"of_parse_phandle|"
    r"of_get_property|"
    r"bus_find_device_by_name|"
    r"class_find_device|"
    r"driver_find_device"
)

_NULLABLE_ASSIGN_RE = re.compile(
    r"^(?P<indent>\s*)"
    # Same declaration-prefix grammar as _ALLOC_ASSIGN_RE.
    r"(?:[\w\s]+?[\s\*]\s*\**\s*)?"
    r"(?P<lhs>[A-Za-z_][\w\.\->]*)\s*=\s*"
    r"(?:\([\w\s\*]*\)\s*)?"  # optional cast
    r"(?P<api>" + _NULLABLE_RETURN_APIS + r")\s*\("
)

# Patterns that count as a deref of x: x->field, *x, x[i],
# OR x passed as a function argument (heuristic).
_DEREF_FIELD_RE_TPL = (
    r"\b{var}\s*->"
)
_DEREF_PASSED_RE_TPL = (
    # A bare `var` passed as a function arg or used in
    # *var.  We require either ',' or '(' immediately
    # before to avoid matching var as part of a larger
    # identifier or as the LHS of an assignment.
    r"(?<![\w\->.])(?:\*\s*)?{var}(?![\w])"
)


def _instrument_null_after_alloc(source: str, fn_name: str
                                 ) -> tuple[str, int]:
    """Apply null_after_alloc fallback to the named function.

    Inserts __assert_safe_to_deref(x) BEFORE each
    dereference (x->field) or function-argument-pass of x,
    where x is the LHS of a previous nullable-API call.

    The contract on __assert_safe_to_deref(p) requires
    `p != NULL || null_check_done(p) == 1`.  CBMC's symex
    handles the path where x is non-NULL (assert holds) and
    the path where x is NULL but the function reached a
    NULL-check guard (assert holds).  Only the genuine
    'NULL-returned-but-no-guard' path produces a contract
    violation.
    """
    body_range = _find_function_body(source, fn_name)
    if body_range is None:
        return source, 0
    body_start, body_end = body_range
    body_text = source[body_start:body_end]
    body_lines = _split_lines_with_offsets(body_text)

    # Pass 1: find allocations whose LHS we'll track.
    tracked_lhs: list[tuple[int, str]] = []  # (alloc-end-offset, lhs)
    for ls, le, line in body_lines:
        m = _NULLABLE_ASSIGN_RE.match(line)
        if not m:
            continue
        lhs = m.group("lhs")
        # Find the ';' terminating this statement.
        j = ls
        depth = 0
        while j < len(body_text):
            c = body_text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == ";" and depth == 0:
                tracked_lhs.append((j + 1, lhs))
                break
            j += 1
    if not tracked_lhs:
        return source, 0

    # Pass 2: for each tracked LHS, find later derefs in
    # the function body and insert
    # __assert_safe_to_deref(<lhs>) before each.  Only
    # consider derefs at offsets STRICTLY AFTER the
    # allocation site so we don't add asserts before the
    # allocation runs.
    edits: list[tuple[int, int, str]] = []
    seen_assert_offsets: set[tuple[int, str]] = set()
    for alloc_end, lhs in tracked_lhs:
        # Bare-identifier check is needed; we don't want to
        # match `lhs` as part of e.g. `lhs_other`.
        # Build safe regex by escaping the LHS.
        esc = re.escape(lhs)
        # Match a deref of `lhs`:
        #   * `lhs->fld`     — pointer field access
        #   * `*lhs`         — explicit dereference
        #   * `lhs[i]`       — array index
        #   * `lhs` passed to a function (heuristic: lhs
        #     bare, preceded by `(` or `,` and followed
        #     by `,` or `)`).
        deref_re = re.compile(
            # Field access
            r"(?<![\w\->.])" + esc + r"\s*->"
            r"|"
            # Array index
            r"(?<![\w\->.])" + esc + r"\s*\["
            r"|"
            # Explicit dereference
            r"(?<![\w])\*\s*" + esc + r"(?![\w])"
            r"|"
            # Passed as function arg: preceded by `(` or `,`
            # (with optional whitespace), followed by `,`
            # or `)` (with optional whitespace), and not
            # adjacent to a `&` (taking the address is fine).
            r"(?<=[(,])\s*" + esc + r"\s*(?=[,)])"
        )
        # Find each deref position in the body AFTER the
        # alloc.
        for dm in deref_re.finditer(body_text, pos=alloc_end):
            deref_off = dm.start()
            # Find the start of the statement containing
            # this offset.  We walk back from deref_off to
            # the first ';' or '{' in the same brace depth.
            j = deref_off
            stmt_start = alloc_end  # sane default
            while j > alloc_end:
                c = body_text[j]
                if c == ";" or c == "{":
                    # The statement starts AFTER this delim
                    # (skip whitespace).
                    k = j + 1
                    while k < len(body_text) and body_text[k] in " \t\n\r":
                        k += 1
                    stmt_start = k
                    break
                j -= 1
            else:
                # Reached alloc_end; statement starts there.
                k = alloc_end
                while k < len(body_text) and body_text[k] in " \t\n\r":
                    k += 1
                stmt_start = k

            # De-duplicate: don't insert multiple asserts at
            # the same statement-start for the same lhs.
            key = (stmt_start, lhs)
            if key in seen_assert_offsets:
                continue
            seen_assert_offsets.add(key)
            # Determine the indent of the statement start
            # by looking at the line containing stmt_start.
            line_start = stmt_start
            while (line_start > 0
                   and body_text[line_start - 1] != "\n"):
                line_start -= 1
            indent = ""
            k = line_start
            while k < len(body_text) and body_text[k] in " \t":
                indent += body_text[k]
                k += 1
            # Insert the assert as its own line BEFORE the
            # statement.
            edits.append((
                stmt_start, stmt_start,
                f"__assert_safe_to_deref({lhs});\n{indent}"
            ))

    if not edits:
        return source, 0
    edits.sort(key=lambda x: (-x[0], -x[1]))
    new_body = body_text
    for start, end, text in edits:
        new_body = new_body[:start] + text + new_body[end:]
    return (source[:body_start] + new_body + source[body_end:],
            len(edits))


_SHAPE_HANDLERS["null_after_alloc"] = _instrument_null_after_alloc


# Match INIT_WORK(&X->fld, ...) and similar work-init calls.
_INIT_WORK_RE = re.compile(
    r"^(?P<indent>\s*)"
    r"(?:INIT_WORK|INIT_DELAYED_WORK|"
    r"__INIT_WORK|INIT_WORK_ONSTACK)\s*\(\s*"
    r"(?P<arg>&?[\w\.\->]+)\s*,"
)
_KFREE_RE = re.compile(
    r"^(?P<indent>\s*)kfree\s*\(\s*(?P<arg>[\w\.\->]+)\s*\)"
)
_CANCEL_WORK_RE = re.compile(
    r"^(?P<indent>\s*)"
    r"(?:cancel_work_sync|cancel_delayed_work_sync|"
    r"flush_work|flush_delayed_work)\s*\(\s*"
    r"(?P<arg>&?[\w\.\->]+)\s*\)"
)


def _instrument_cancel_work(source: str, fn_name: str
                            ) -> tuple[str, int]:
    """Apply cancel_work_before_free fallback: insert
    `cancel_work_set_pending(W)` after each INIT_WORK(W, ...),
    `cancel_work_clear_pending(W)` after each cancel_work_sync,
    and `__assert_no_pending_work(W)` before each kfree
    of an object known to embed a tracked work_struct."""
    body_range = _find_function_body(source, fn_name)
    if body_range is None:
        return source, 0
    body_start, body_end = body_range
    body_text = source[body_start:body_end]
    body_lines = _split_lines_with_offsets(body_text)

    # Pass 1: collect INIT_WORK args (the work_struct pointers
    # we'll later check before kfree).
    tracked_works: list[str] = []  # exprs like '&obj->work'

    edits: list[tuple[int, int, str]] = []
    for ls, le, line in body_lines:
        m = _INIT_WORK_RE.match(line)
        if m:
            arg = m.group("arg")
            tracked_works.append(arg)
            # Find statement-terminating ';' so we insert AFTER.
            j = ls
            depth = 0
            while j < len(body_text):
                c = body_text[j]
                if c == "(":
                    depth += 1
                elif c == ")":
                    depth -= 1
                elif c == ";" and depth == 0:
                    edits.append((
                        j + 1, j + 1,
                        f"\n{m.group('indent')}"
                        f"cancel_work_set_pending({arg});"
                    ))
                    break
                j += 1
            continue
        m = _CANCEL_WORK_RE.match(line)
        if m:
            arg = m.group("arg")
            j = ls
            depth = 0
            while j < len(body_text):
                c = body_text[j]
                if c == "(":
                    depth += 1
                elif c == ")":
                    depth -= 1
                elif c == ";" and depth == 0:
                    edits.append((
                        j + 1, j + 1,
                        f"\n{m.group('indent')}"
                        f"cancel_work_clear_pending({arg});"
                    ))
                    break
                j += 1
            continue
        m = _KFREE_RE.match(line)
        if m and tracked_works:
            arg = m.group("arg")
            indent = m.group("indent")
            # Insert __assert_no_pending_work before each
            # tracked work_struct that's reachable from arg
            # (heuristic: starts with &arg-> or &arg.).
            prefix_a = f"&{arg}->"
            prefix_b = f"&{arg}."
            relevant = [w for w in tracked_works
                        if w.startswith(prefix_a)
                        or w.startswith(prefix_b)]
            if not relevant:
                continue
            # Find the kfree's terminating ';'.
            j = ls
            depth = 0
            cstart = ls + len(indent)
            while j < len(body_text):
                c = body_text[j]
                if c == "(":
                    depth += 1
                elif c == ")":
                    depth -= 1
                elif c == ";" and depth == 0:
                    stmt = body_text[cstart:j + 1]
                    asserts = "".join(
                        f"__assert_no_pending_work({w}); "
                        for w in relevant)
                    edits.append((
                        cstart, j + 1,
                        "{ " + asserts + stmt + " }"
                    ))
                    break
                j += 1

    edits.sort(key=lambda x: (-x[0], -x[1]))
    new_body = body_text
    for start, end, text in edits:
        new_body = new_body[:start] + text + new_body[end:]

    return (source[:body_start] + new_body + source[body_end:],
            len(edits))


_SHAPE_HANDLERS["cancel_work_before_free"] = _instrument_cancel_work


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--function", required=True)
    ap.add_argument("--shape", action="append", required=True,
                    choices=sorted(_SHAPE_HANDLERS))
    args = ap.parse_args(argv)

    source = args.input.read_text()
    total_inserts = 0
    for shape in args.shape:
        handler = _SHAPE_HANDLERS[shape]
        source, n = handler(source, args.function)
        total_inserts += n

    args.output.write_text(source)
    print(f"  fallback-instrumented: {total_inserts} insertion(s)",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
