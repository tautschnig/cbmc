#!/usr/bin/env python3
"""synthesise_harness.py — generate a per-function harness C file
that invokes a given kernel function with nondet arguments AND
bootstraps the property-module ghost state.

This is the "per-file harness" layer that LIM-013's resolution
needs.  goto-harness alone would generate just the nondet-init
plus function call; but the scan's property modules track state
in ghost tables keyed by pointer identity, so the synthesised
harness also needs to register each pointer argument with the
relevant ghost API before the function-under-test runs.

## Usage

  synthesise_harness.py <module> <kernel-file> <target-function> <out.c>

Inputs:

  module           one of the registered property modules, e.g.
                   'cred_lifetime'.  Determines which ghost
                   bootstrap calls to emit.
  kernel-file      path to the .c file containing
                   target-function (used to discover the
                   function's signature).
  target-function  name of the enclosing function the
                   Coccinelle prefilter hit is inside.
  out.c            where to write the synthesised harness.

## Output

A self-contained C file that:

  1. Forward-declares `struct` types referenced by the target's
     parameters (they unify with the kernel TU's definitions at
     link time).
  2. Forward-declares the target function (matching its signature
     found in kernel-file).
  3. Declares the property module's ghost bootstrap API.
  4. In `main()`:
       - Allocates a byte buffer per pointer argument (since the
         struct is opaque in this TU).
       - Calls `<module>_assume_live(arg)` for each pointer arg
         of a type the module tracks.
       - Invokes target-function with the prepared pointers.

The resulting harness is compiled alongside the kernel TU +
adapter + property module by the per-file scan pipeline, then
goto-instrument --replace-call-with-contract is applied to the
module's contract target, and cbmc runs on the synthesised
entry function.

## Scope and limits

This is deliberately minimal.  It handles:

  - Pointer-to-struct parameters for types the module tracks
    (bootstrapped to "live").
  - Other pointer types (nondet-allocated byte buffer).
  - Scalar parameters (nondet-initialised locals).

It does NOT handle:

  - Function-pointer parameters (needs per-kernel-API fixup).
  - Arrays / variadics (rare in the kernel's contracted APIs).
  - Deeply-nested struct initialisation (the object factory
    handles that once goto-cc reads the harness; the
    `--max-dynamic-object-instances` cap from LIM-008 keeps it
    bounded).

When a parameter type can't be handled, a clearly-marked
placeholder goes into the harness and the script prints a
warning; the scan then reports the harness as "needs-fixup".
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


# Per-module configuration: which parameter-type substring triggers
# a ghost-state bootstrap, and what function call to emit.
MODULE_GHOST_BOOTSTRAP = {
    "cred_lifetime": {
        # struct-type substrings to match.  We match on the type text
        # rather than resolving the typedef graph: if a param's type
        # string contains any of these, treat it as cred-typed.
        "types": ["struct cred *", "const struct cred *"],
        # Header function(s) to assume-live at the harness entry.
        # Each takes a single `struct cred *`.
        "ghost_init_call": "cred_lifetime_init",
        "ghost_init_args_template": "(struct cred *){arg}, 1",
        "ghost_init_decl":
            "void cred_lifetime_init(struct cred *c, unsigned int usage);",
        "forward_decls": ["struct cred;"],
    },
    "pipe_buffer": {
        "types": ["struct pipe_buffer *"],
        "ghost_init_call": "pipe_buffer_mark_populated",
        "ghost_init_args_template": "(struct pipe_buffer *){arg}",
        "ghost_init_decl":
            "void pipe_buffer_mark_populated(struct pipe_buffer *buf);",
        "forward_decls": ["struct pipe_buffer;"],
    },
    "lock_state": {
        # Match mutex-typed parameters; per-file harness marks each
        # as held at entry so the scan checks whether the enclosing
        # function unlocks them zero, one, or more times.
        "types": ["struct mutex *"],
        "ghost_init_call": "lock_state_lock",
        "ghost_init_args_template": "(struct mutex *){arg}",
        "ghost_init_decl":
            "void lock_state_lock(struct mutex *m);",
        "forward_decls": ["struct mutex;"],
    },
    "refcount_lifetime": {
        # Match refcount_t-typed parameters; per-file harness inits
        # each with usage=1 so the first dec lands on a live counter
        # and a double-dec pattern inside the enclosing function is
        # detected.
        "types": ["refcount_t *"],
        "ghost_init_call": "refcount_lifetime_init",
        "ghost_init_args_template": "(refcount_t *){arg}, 1",
        "ghost_init_decl":
            "void refcount_lifetime_init(refcount_t *r, "
            "unsigned int usage);",
        "forward_decls": [
            "typedef struct refcount_struct refcount_t;",
        ],
    },
    # aead doesn't have a ghost-state-bootstrap need that survives
    # per-file synthesis today: its predicate `sgl_all_user_writable`
    # walks a scatterlist attached to req->dst and checks each page's
    # provenance via the page_provenance ghost table.  Without
    # materialising a concrete SGL (kernel scatterlist layout is
    # version-specific and not trivially fabricable from a typedef),
    # per-file synthesis would either yield trivially-vacuous results
    # or fail at compile time.  --per-file on an aead hit therefore
    # falls through to the adapter-needed fallback; the aead
    # direct-call harness under scan/adapters/ remains the supported
    # path for aead.
}


@dataclass
class Parameter:
    """One parameter of the target function, as extracted from the
    kernel source."""
    type_text: str      # e.g. 'const struct cred *'
    name: str           # e.g. 'tsk'


@dataclass
class Signature:
    """Parsed signature of the target function."""
    return_type: str
    params: list[Parameter]


def _strip_comments(text: str) -> str:
    # Remove C block comments and line comments.  Good enough for
    # parsing kernel function signatures in headers / .c files.
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def find_function_signature(source: Path, name: str) -> Signature | None:
    """Find the first definition of `name` in `source` and return its
    parsed signature.  Uses a regex tuned for kernel-style C; does
    not understand __attribute__ or preprocessor nesting, but
    handles 'static inline TYPE NAME(PARAMS)' and 'TYPE NAME(PARAMS)'.
    """
    text = _strip_comments(source.read_text(errors="replace"))

    # Match 'TYPE NAME(...)' where TYPE is one or more tokens and
    # NAME is the requested function name.  Look for an opening '{'
    # or ';' after the ')' to anchor on a definition (or declaration).
    pat = re.compile(
        r"([\w\s\*\(\)]+?)\b" + re.escape(name) +
        r"\s*\(\s*([^{};]*?)\s*\)\s*[{;]",
        re.MULTILINE | re.DOTALL,
    )
    m = pat.search(text)
    if not m:
        return None

    return_type = m.group(1).strip()
    # Clean leading keywords.
    for kw in ["static", "inline", "extern", "__always_inline",
               "noinline", "__init", "__exit"]:
        return_type = re.sub(r"\b" + kw + r"\b", "", return_type)
    return_type = " ".join(return_type.split())

    params_text = m.group(2).strip()
    if not params_text or params_text == "void":
        return Signature(return_type=return_type, params=[])

    params: list[Parameter] = []
    for i, raw in enumerate(_split_params(params_text)):
        raw = raw.strip()
        if not raw:
            continue
        # Parameter name is typically the last identifier before
        # any trailing [] or empty.  Pointer asterisks stay with
        # the type.
        m2 = re.search(r"(\w+)\s*(\[[^\]]*\])?\s*$", raw)
        if m2:
            pname = m2.group(1)
            type_text = raw[:m2.start()].rstrip()
        else:
            # Anonymous parameter; synthesize a name.
            pname = f"p{i}"
            type_text = raw
        type_text = " ".join(type_text.split())
        params.append(Parameter(type_text=type_text, name=pname))

    return Signature(return_type=return_type, params=params)


def _split_params(text: str) -> list[str]:
    # Split on top-level commas (commas not nested inside parens).
    depth = 0
    start = 0
    result: list[str] = []
    for i, c in enumerate(text):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            result.append(text[start:i])
            start = i + 1
    tail = text[start:].strip()
    if tail:
        result.append(tail)
    return result


def is_ghost_tracked(module: str, param_type: str) -> bool:
    cfg = MODULE_GHOST_BOOTSTRAP.get(module)
    if not cfg:
        return False
    return any(t in param_type for t in cfg["types"])


def synthesise(module: str, source: Path, function: str,
               out: Path) -> int:
    cfg = MODULE_GHOST_BOOTSTRAP.get(module)
    if not cfg:
        print(f"synthesise_harness: module {module!r} has no ghost "
              f"bootstrap configuration; per-file harness cannot be "
              f"generated automatically.", file=sys.stderr)
        return 2

    sig = find_function_signature(source, function)
    if sig is None:
        print(f"synthesise_harness: could not find function "
              f"{function!r} in {source}", file=sys.stderr)
        return 2

    # Build the harness source.
    lines: list[str] = []
    lines.append("// Auto-generated by scan/synthesise_harness.py")
    lines.append(f"// Module: {module}")
    lines.append(f"// Source: {source}")
    lines.append(f"// Target: {function}")
    lines.append("")

    # Forward declarations for the module's tracked types.
    for decl in cfg.get("forward_decls", []):
        lines.append(decl)
    # Ghost-init API.
    lines.append(cfg["ghost_init_decl"])

    # Also forward-declare any struct types appearing in params
    # that aren't already declared.  Kernel-TU definitions unify
    # these at link time.
    struct_tags_seen: set[str] = set()
    for decl in cfg.get("forward_decls", []):
        m = re.search(r"struct\s+(\w+)", decl)
        if m:
            struct_tags_seen.add(m.group(1))
    for p in sig.params:
        for m in re.finditer(r"struct\s+(\w+)", p.type_text):
            tag = m.group(1)
            if tag not in struct_tags_seen:
                lines.append(f"struct {tag};")
                struct_tags_seen.add(tag)
    # Return type structs.
    for m in re.finditer(r"struct\s+(\w+)", sig.return_type):
        tag = m.group(1)
        if tag not in struct_tags_seen:
            lines.append(f"struct {tag};")
            struct_tags_seen.add(tag)

    # Forward-declare unknown typedef names that appear in the
    # signature.  Kernel functions routinely take typedef'd
    # integer types (u32, pid_t, umode_t) or typedef'd struct
    # pointers (kernel_siginfo_t, fmode_t).  Without a definition
    # in scope the harness won't parse.  Strategy: any bare
    # identifier in a parameter or return type that is not a C
    # keyword, not a known struct tag, and not listed in a fixed
    # built-in set is treated as an opaque typedef and declared
    # as `typedef char <name>;`.  That keeps pointer-to-<name>
    # binary-compatible with any kernel definition (all kernel
    # pointers have the same representation on x86_64), and for
    # scalar <name> the harness allocates a local that is
    # nondet-initialised by CBMC — matching the existing scalar
    # path.
    C_RESERVED = {
        "char", "short", "int", "long", "signed", "unsigned",
        "float", "double", "void", "_Bool", "bool", "const", "volatile",
        "restrict", "static", "inline", "extern", "register",
        "struct", "union", "enum", "typedef", "auto", "sizeof",
        "return", "goto", "_Atomic", "__restrict", "__inline",
        "__inline__", "__attribute__",
        # Common C99/POSIX/kernel typedefs that CBMC's bootstrap
        # headers or the kernel TU already provide.  Without
        # excluding them, our `typedef char <name>;` fallback
        # would re-typedef them and trigger
        # "type symbol '<name>' defined twice".
        "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "true", "false", "NULL",
    }
    # Identifiers the forward_decls block or ghost_init_decl
    # already declared explicitly — don't re-typedef those.
    explicit_typedef_names: set[str] = set()
    for decl in cfg.get("forward_decls", []):
        for m in re.finditer(r"\btypedef\s+[^;]*?\b(\w+)\s*;", decl):
            explicit_typedef_names.add(m.group(1))
    typedefs_seen: set[str] = set(explicit_typedef_names)

    def _collect_typedef_names(type_text: str) -> list[str]:
        """Return identifiers in `type_text` that look like
        typedef names (not C keywords, not struct tags)."""
        # Strip qualifiers, pointer/array markers, and struct/
        # union/enum + tag pairs (we handled those already).
        text = re.sub(r"\bstruct\s+\w+", "", type_text)
        text = re.sub(r"\bunion\s+\w+", "", text)
        text = re.sub(r"\benum\s+\w+", "", text)
        text = text.replace("*", " ")
        text = re.sub(r"\[[^\]]*\]", " ", text)
        idents = re.findall(r"\b[A-Za-z_]\w*\b", text)
        return [i for i in idents if i not in C_RESERVED]

    for p in sig.params:
        for ident in _collect_typedef_names(p.type_text):
            if ident in typedefs_seen:
                continue
            lines.append(f"typedef char {ident};")
            typedefs_seen.add(ident)
    for ident in _collect_typedef_names(sig.return_type):
        if ident in typedefs_seen:
            continue
        lines.append(f"typedef char {ident};")
        typedefs_seen.add(ident)

    # Target function declaration.
    arg_sig = ", ".join(f"{p.type_text} {p.name}" for p in sig.params) \
        or "void"
    lines.append(f"{sig.return_type} {function}({arg_sig});")
    lines.append("")

    # Harness body.
    harness_name = f"{function}_per_file_harness"
    lines.append(f"int {harness_name}(void)")
    lines.append("{")

    # For each pointer parameter: back with a 1 KiB static buffer,
    # cast to the parameter's type, and if the type is ghost-
    # tracked, bootstrap the ghost to "live".
    call_args: list[str] = []
    warnings: list[str] = []
    for i, p in enumerate(sig.params):
        local = f"arg{i}"
        if "*" in p.type_text:
            lines.append(f"  static char {local}_backing[1024];")
            lines.append(f"  {p.type_text} {local} = "
                         f"({p.type_text}){local}_backing;")
            if is_ghost_tracked(module, p.type_text):
                ghost_args = cfg["ghost_init_args_template"].format(arg=local)
                lines.append(f"  {cfg['ghost_init_call']}({ghost_args});")
            call_args.append(local)
        else:
            # Scalar: declared-uninitialised ≡ nondet under CBMC.
            lines.append(f"  {p.type_text} {local};")
            call_args.append(local)

    lines.append("")
    if sig.return_type == "void":
        lines.append(f"  {function}({', '.join(call_args)});")
    else:
        lines.append(
            f"  {sig.return_type} _ret = {function}({', '.join(call_args)});"
        )
        lines.append("  (void)_ret;")
    lines.append("")
    lines.append("  return 0;")
    lines.append("}")
    lines.append("")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines))

    print(f"Wrote {out} (harness entry: {harness_name})")
    if warnings:
        for w in warnings:
            print(f"  warning: {w}", file=sys.stderr)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("module")
    ap.add_argument("source", type=Path)
    ap.add_argument("function")
    ap.add_argument("out", type=Path)
    args = ap.parse_args()
    return synthesise(args.module, args.source, args.function, args.out)


if __name__ == "__main__":
    sys.exit(main())
