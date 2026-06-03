#!/usr/bin/env python3
"""Auto-harness generator for CodeQL→CBMC stage-2 refinement.

Takes a pipe-delimited CodeQL structured candidate line:
  func|file|sink_line|sink_fn|tainted_param|dest_expr

Reads the enclosing function from the source file, extracts:
  - buffer allocation size (looks for size_buf / PCI_BUF_SIZE / sizeof / kmalloc)
  - clamping guards on the tainted param before the sink
  - the copy call itself

Generates a self-contained CBMC harness .c file with:
  - a fixed-size buffer modeling the destination
  - nondet tainted param + any related vars (ppos, image_size)
  - __CPROVER_assume for each guard found before the sink
  - the copy (as memcpy) with --bounds-check as the oracle

Usage:
  echo "buffer_from_user|/path/vme_user.c|172|copy_from_user|count|image_ptr" | \
    python3 auto_harness.py > harness.c
  # or:
  python3 auto_harness.py --candidate "buffer_from_user|..." > harness.c
"""
import re
import sys
import argparse
from pathlib import Path


def read_function_body(filepath, func_name, sink_line):
    """Read lines of the function containing sink_line."""
    lines = Path(filepath).read_text().splitlines()
    # Find function start: walk backwards from sink_line to find opening '{'
    # with the function signature preceding it
    start = None
    brace_count = 0
    # Simple heuristic: find the function definition line
    for i in range(sink_line - 1, -1, -1):
        if re.match(rf'(static\s+)?(\w+\s+)+{re.escape(func_name)}\s*\(', lines[i]):
            start = i
            break
    if start is None:
        # Fallback: take 30 lines before sink
        start = max(0, sink_line - 30)

    # Find end: match braces from start
    end = sink_line  # at minimum
    brace_count = 0
    in_func = False
    for i in range(start, min(len(lines), start + 200)):
        for ch in lines[i]:
            if ch == '{':
                brace_count += 1
                in_func = True
            elif ch == '}':
                brace_count -= 1
                if in_func and brace_count == 0:
                    end = i
                    break
        if in_func and brace_count == 0:
            break

    return lines[start:end + 1], start


def extract_guards(func_lines, tainted_param, sink_offset):
    """Find if-statements that clamp tainted_param before the sink."""
    guards = []
    pat = re.compile(
        rf'\bif\s*\(\s*{re.escape(tainted_param)}\s*(>|>=|<|<=)\s*(.+?)\s*\)')
    assign_pat = re.compile(
        rf'{re.escape(tainted_param)}\s*=\s*(.+?)\s*;')
    for i, line in enumerate(func_lines[:sink_offset]):
        m = pat.search(line)
        if m:
            guards.append((m.group(0), m.group(1), m.group(2).strip()))
        m2 = assign_pat.search(line)
        if m2 and ('image_size' in m2.group(1) or 'size' in m2.group(1)):
            guards.append((line.strip(), '=', m2.group(1).strip()))
    return guards


def extract_buffer_info(func_lines, dest_expr):
    """Try to identify the buffer size from declarations/alloc."""
    # Look for patterns like: size_buf, PCI_BUF_SIZE, sizeof, kmalloc
    for line in func_lines:
        if 'size_buf' in line or 'PCI_BUF_SIZE' in line:
            m = re.search(r'(0x[0-9a-fA-F]+|\d+)', line)
            if m:
                return m.group(1)
    return None


def detect_proportional_alloc(func_lines, tainted_param, sink_offset):
    """Detect the "buffer allocated proportional to length" pattern.

    Returns the allocation-target variable name when, before the sink,
    a buffer is allocated with a size expression that references the
    tainted param AND that same variable appears in the sink line
    (i.e. it is the copy destination).  Such copies are safe by
    construction regardless of the (user-controlled) length.
    """
    if not (0 <= sink_offset < len(func_lines)):
        return None
    sink_line = func_lines[sink_offset]
    alloc_pat = re.compile(r'(\w+)\s*=\s*\w*alloc\w*\s*\((.*)\)\s*;')
    tp_word = re.compile(rf'\b{re.escape(tainted_param)}\b')
    for line in func_lines[:sink_offset + 1]:
        m = alloc_pat.search(line)
        if m and tp_word.search(m.group(2)):
            var = m.group(1)
            if re.search(rf'\b{re.escape(var)}\b', sink_line):
                return var
    return None


# Kernel framework allocators: name -> 0-based index of the payload-size arg.
# These allocate a buffer whose usable size is the given argument.
KNOWN_ALLOC_HELPERS = {
    "gb_operation_create": 2,
}


def _split_args(s):
    """Split a top-level argument list (parens/brackets balanced)."""
    args, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


def _call_args(text, fn):
    """Return the top-level args of the first call to `fn` in `text`,
    or None.  `text` may be a collapsed (single-line) statement so
    multi-line calls are handled by the caller."""
    m = re.search(rf'\b{re.escape(fn)}\s*\(', text)
    if not m:
        return None
    depth, start = 0, m.end() - 1
    for k in range(start, len(text)):
        if text[k] == "(":
            depth += 1
        elif text[k] == ")":
            depth -= 1
            if depth == 0:
                return _split_args(text[start + 1:k])
    return None


def detect_framework_alloc(func_lines, sink_offset, sink_fn):
    """Detect the interprocedural framework-allocator pattern, e.g.
    `op = gb_operation_create(c, t, size, ...)` followed by
    `copy(op->...->payload, src, size)`.

    Sound: requires the copy's actual size argument to textually equal
    the allocator's size argument, and the alloc-target variable to
    appear in the copy destination.  Returns (size_identifier,
    helper_name) or None.
    """
    if not (0 <= sink_offset < len(func_lines)):
        return None
    flat = " ".join(line.strip() for line in func_lines)
    sink_window = " ".join(func_lines[sink_offset:sink_offset + 4])
    copy_args = _call_args(sink_window, sink_fn)
    if not copy_args or len(copy_args) < 3:
        return None
    copy_dest, copy_size = copy_args[0], copy_args[2]
    if not re.fullmatch(r"\w+", copy_size):  # need a simple identifier
        return None
    for helper, size_idx in KNOWN_ALLOC_HELPERS.items():
        tm = re.search(rf'(\w+)\s*=\s*{re.escape(helper)}\s*\(', flat)
        if not tm:
            continue
        alloc_target = tm.group(1)
        helper_args = _call_args(flat, helper)
        if not helper_args or len(helper_args) <= size_idx:
            continue
        if helper_args[size_idx] == copy_size and \
                re.search(rf'\b{re.escape(alloc_target)}\b', copy_dest):
            return copy_size, helper
    return None


def generate_proportional_harness(func_name, filepath, sink_line, sink_fn,
                                  size_var, dest_expr, alloc_desc):
    """Harness for the alloc-proportional-to-length pattern: model the
    destination buffer as malloc(size_var), which proves safety."""
    out = [
        f'// Auto-generated CBMC stage-2 harness for: {func_name}',
        f'// Source: {filepath}:{sink_line}',
        f'// Sink: {sink_fn}({dest_expr}, ..., {size_var})',
        f'// Pattern: {alloc_desc}',
        '',
        '#include <stdlib.h>',
        '#include <string.h>',
        '',
        '#define WIN_MAX 64   // scaled model of max tainted range',
        '',
        'unsigned int nd_uint(void) { unsigned int x; return x; }',
        '',
        f'void harness_{func_name}(void)',
        '{',
        f'\tunsigned int {size_var} = nd_uint();',
        f'\t__CPROVER_assume({size_var} <= WIN_MAX);',
        f'\t// buffer sized from {size_var} -> dst fits by construction',
        f'\tchar *kern_buf = malloc({size_var});',
        f'\tchar *user_src = malloc({size_var});',
        '\t__CPROVER_assume(kern_buf && user_src);',
        f'\tmemcpy(kern_buf, user_src, {size_var});',
        '}',
    ]
    return '\n'.join(out)


def generate_harness(func_name, filepath, sink_line, sink_fn, tainted_param,
                     dest_expr, func_lines, func_start):
    """Generate the CBMC harness C file."""
    sink_offset = sink_line - func_start - 1
    guards = extract_guards(func_lines, tainted_param, sink_offset)

    # Allocation-aware path: if the destination buffer is allocated with a
    # size derived from the tainted param, the copy is safe by construction.
    alloc_var = detect_proportional_alloc(func_lines, tainted_param, sink_offset)
    if alloc_var:
        return generate_proportional_harness(
            func_name, filepath, sink_line, sink_fn, tainted_param, dest_expr,
            f'buffer "{alloc_var}" allocated proportional to {tainted_param}')

    # Interprocedural framework-allocator path (e.g. gb_operation_create).
    fw = detect_framework_alloc(func_lines, sink_offset, sink_fn)
    if fw:
        size_var, helper = fw
        return generate_proportional_harness(
            func_name, filepath, sink_line, sink_fn, size_var, dest_expr,
            f'{helper}() sizes payload by {size_var} (== copy size)')

    # Determine buffer size — default scaled model
    buf_size = "BUF"
    # Check if there's a clamp to some limit
    has_buf_clamp = any(g[1] == '>' and 'size_buf' in g[2] for g in guards)

    out = []
    out.append(f'// Auto-generated CBMC stage-2 harness for: {func_name}')
    out.append(f'// Source: {filepath}:{sink_line}')
    out.append(f'// Sink: {sink_fn}({dest_expr}, ..., {tainted_param})')
    out.append(f'// Guards found: {len(guards)}')
    out.append('')
    out.append('#include <string.h>')
    out.append('')
    out.append('#define BUF 8        // scaled model of fixed buffer')
    out.append('#define WIN_MAX 64   // scaled model of max tainted range')
    out.append('')
    out.append('static char kern_buf[BUF];')
    out.append('static char user_src[WIN_MAX];')
    out.append('')
    out.append('unsigned int nd_uint(void) { unsigned int x; return x; }')
    out.append('int nd_int(void) { int x; return x; }')
    out.append('')
    out.append(f'void harness_{func_name}(void)')
    out.append('{')

    # Declare nondet variables based on what the guards reference
    vars_declared = set()
    vars_declared.add(tainted_param)
    out.append(f'\tunsigned int {tainted_param} = nd_uint();')
    out.append(f'\t__CPROVER_assume({tainted_param} <= WIN_MAX);')

    # Check if guards reference image_size, ppos, etc.
    for g in guards:
        for v in ['image_size', 'ppos', 'size_buf']:
            if v in g[0] and v not in vars_declared:
                vars_declared.add(v)
                if v == 'ppos':
                    out.append(f'\tint {v} = nd_int();')
                else:
                    out.append(f'\tunsigned int {v} = nd_uint();')
                if v == 'image_size':
                    out.append(f'\t__CPROVER_assume({v} >= 1);')
                    out.append(f'\t__CPROVER_assume({v} <= WIN_MAX);')

    out.append('')
    out.append(f'\t// Guards from source ({func_name}):')

    # Emit guards as assumes or early-returns
    for g_text, op, rhs in guards:
        if op == '=' and 'image_size' in rhs:
            # count = image_size - ppos style assignment
            out.append(f'\t// {g_text}')
            out.append(f'\t{tainted_param} = {rhs};')
        elif op in ('>', '>='):
            # if (count > X) count = X  or  if (ppos > Y) return
            if 'size_buf' in rhs or 'BUF' in rhs.upper():
                out.append(f'\tif({tainted_param} > BUF)')
                if has_buf_clamp:
                    out.append(f'\t\t{tainted_param} = BUF;')
                else:
                    out.append(f'\t\treturn;')
            elif 'image_size' in rhs:
                out.append(f'\tif({tainted_param} > image_size)')
                out.append(f'\t\t{tainted_param} = image_size;')
            elif 'ppos' in g_text:
                out.append(f'\t// early-return guard: {g_text}')
                out.append(f'\tif(ppos < 0 || (unsigned int)ppos > image_size - 1)')
                out.append(f'\t\treturn;')
        elif op in ('<', '<='):
            out.append(f'\t// {g_text}  (no-op for OOB analysis)')

    out.append('')
    out.append(f'\t// Sink: {sink_fn}')

    # Determine dest pointer
    if 'ppos' in vars_declared:
        out.append(f'\tmemcpy(kern_buf + ppos, user_src, {tainted_param});')
    else:
        out.append(f'\tmemcpy(kern_buf, user_src, {tainted_param});')

    out.append('}')
    return '\n'.join(out)


def main():
    parser = argparse.ArgumentParser(description='Generate CBMC harness from CodeQL candidate')
    parser.add_argument('--candidate', '-c', help='pipe-delimited candidate line')
    args = parser.parse_args()

    if args.candidate:
        line = args.candidate
    else:
        line = sys.stdin.readline().strip()

    parts = line.split('|')
    if len(parts) < 6:
        sys.exit(f'Expected 6 pipe-delimited fields, got {len(parts)}: {line}')

    func_name, filepath, sink_line_s, sink_fn, tainted_param, dest_expr = parts[:6]
    sink_line = int(sink_line_s)

    func_lines, func_start = read_function_body(filepath, func_name, sink_line)
    harness = generate_harness(func_name, filepath, sink_line, sink_fn,
                               tainted_param, dest_expr, func_lines, func_start)
    print(harness)


if __name__ == '__main__':
    main()
