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
        # Wrapper-struct pointer chains.  Each entry maps a
        # parameter type that the function might receive (a
        # wrapper struct) to the field path that contains a
        # cred *.  When matched, the synthesiser also bootstraps
        # the field pointer.
        #
        # The kernel_includes list pulls in the header that
        # defines the wrapper struct so the harness can resolve
        # the field access.  The structural-equivalence-aware
        # linker (see commit 0414b43bf2) and the matched-
        # KBUILD_MODNAME harness compile (see scan-per-file.sh)
        # make these wrapper paths link cleanly even with the
        # broad header chains they pull in.
        "wrapper_paths": [
            {
                # nlmclnt_release_host(struct nlm_host *) and
                # friends carry the client's cred via h_cred.
                "param_type": "struct nlm_host *",
                "field_path": "h_cred",
                "kernel_includes": ["<linux/lockd/lockd.h>"],
            },
        ],
    },
    "pipe_buffer": {
        "types": ["struct pipe_buffer *"],
        "ghost_init_call": "pipe_buffer_mark_populated",
        "ghost_init_args_template": "(struct pipe_buffer *){arg}",
        "ghost_init_decl":
            "void pipe_buffer_mark_populated(struct pipe_buffer *buf);",
        "forward_decls": ["struct pipe_buffer;"],
        "wrapper_paths": [],
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
        "wrapper_paths": [
            {
                # __pipe_unlock(struct pipe_inode_info *) and
                # related pipe-locking helpers operate on the
                # embedded mutex via &pipe->mutex.  pipe_fs_i.h
                # uses wait_queue_head_t but does not pull in
                # <linux/wait.h> itself, and embeds `struct
                # mutex` so <linux/mutex.h> must precede it.
                "param_type": "struct pipe_inode_info *",
                "field_path": "&{arg}->mutex",
                "kernel_includes": [
                    "<linux/mutex.h>",
                    "<linux/wait.h>",
                    "<linux/pipe_fs_i.h>",
                ],
            },
            {
                # nlm_host_rebooted, nlm_destroy_host_locked etc.
                # operate on the embedded h_mutex via
                # &host->h_mutex.
                "param_type": "struct nlm_host *",
                "field_path": "&{arg}->h_mutex",
                "kernel_includes": ["<linux/lockd/lockd.h>"],
            },
        ],
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
        "wrapper_paths": [
            {
                # nlm_release_host etc. take the wrapper and
                # operate on its h_count refcount.
                "param_type": "struct nlm_host *",
                "field_path": "&{arg}->h_count",
                "kernel_includes": ["<linux/lockd/lockd.h>"],
            },
        ],
    },
    # aead per-file is supported via a custom multi-statement
    # bootstrap: the synthesised harness includes <crypto/aead.h>
    # for the aead_request layout, allocates a 1-element
    # scatterlist on a page-aligned backing buffer, marks the
    # page as PAGE_USER_WRITABLE in the page_provenance ghost,
    # and assigns req->dst to the SGL.  This produces a request
    # whose contract precondition (`sgl_all_user_writable(dst)`)
    # holds at entry; if the enclosing function reassigns
    # req->dst before calling the contracted API, the verdict
    # depends on the new SGL's provenance.
    #
    # The custom_setup_template is emitted instead of the
    # ghost_init_call when synthesise_harness encounters a
    # parameter whose type matches `types`.  `{arg}` is the
    # parameter local; `{i}` is the parameter index used to
    # disambiguate static backing buffers when the harness has
    # multiple aead_request * parameters.
    "aead": {
        "types": ["struct aead_request *"],
        "custom_setup": True,
        # Statements inserted into the harness body for each
        # matched parameter.  Indented by 2 spaces because the
        # synthesiser puts them inside the harness function body.
        "custom_setup_template": (
            "  static char arg{i}_page_backing[4096] "
            "__attribute__((aligned(8)));\n"
            "  static struct scatterlist arg{i}_sgl[1];\n"
            "  set_page_prov("
            "(struct page *)arg{i}_page_backing, PAGE_USER_WRITABLE);\n"
            "  arg{i}_sgl[0].page_link = "
            "(unsigned long)arg{i}_page_backing | 2u;\n"
            "  arg{i}_sgl[0].offset = 0;\n"
            "  arg{i}_sgl[0].length = sizeof(arg{i}_page_backing);\n"
            "  {arg}->dst = &arg{i}_sgl[0];\n"
            "  {arg}->src = &arg{i}_sgl[0];"
        ),
        "ghost_init_decl": "",
        # Force the harness preamble to include kernel headers
        # rather than emitting forward decls — we need the real
        # struct aead_request and struct scatterlist layouts so
        # the field assignment compiles and matches the linked
        # kernel TU's view.
        "forward_decls": [
            "#include <crypto/aead.h>",
            "#include <linux/scatterlist.h>",
            "typedef enum {",
            "  PAGE_PROV_UNSET = 0,",
            "  PAGE_USER_WRITABLE,",
            "  PAGE_CACHE_RO,",
            "  PAGE_KERNEL_ONLY,",
            "} page_provenance_t;",
            "void set_page_prov(struct page *p, page_provenance_t prov);",
        ],
    },
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
    is_static: bool = False


def _strip_comments(text: str) -> str:
    # Remove C block comments and line comments.  Good enough for
    # parsing kernel function signatures in headers / .c files.
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    # Remove preprocessor directives (#ifdef, #define, #include,
    # #if, #else, #endif, etc.).  Without this, a function guarded
    # by `#ifdef CONFIG_FOO` would have the CONFIG_FOO identifier
    # visible to the regex-based typedef-fallback, which would then
    # emit `typedef char CONFIG_FOO;` in the synthesised harness —
    # a syntax error because CONFIG_FOO is typically `#define`d to
    # a numeric constant, not a type name.  Preserve newlines so
    # line numbers stay accurate.
    text = re.sub(r"(?m)^\s*#[^\n]*", "", text)
    return text


def find_function_signature(source: Path, name: str) -> Signature | None:
    """Find the first definition of `name` in `source` and return its
    parsed signature.  Uses a regex tuned for kernel-style C; does
    not understand __attribute__ or preprocessor nesting, but
    handles 'static inline TYPE NAME(PARAMS)' and 'TYPE NAME(PARAMS)'.
    """
    text = _strip_comments(source.read_text(errors="replace"))

    # Match 'TYPE NAME(...)' where TYPE is one or more tokens and
    # NAME is the requested function name.  Anchor on either the
    # start of text, end of line, or a `;` / `}` token so we only
    # match function-definition contexts and not call sites or
    # declarations buried inside expressions.  Require a `{` after
    # the parameter list so we match the definition, not a forward
    # declaration.
    pat = re.compile(
        r"(?:^|[;}\n])"
        r"\s*([\w\s\*\(\)]+?)\b" + re.escape(name) +
        r"\s*\(\s*([^{};]*?)\s*\)\s*\{",
        re.MULTILINE | re.DOTALL,
    )
    m = pat.search(text)
    if not m:
        # Fall back to the looser anchor (no `{` requirement) so
        # we can still find functions whose bodies are
        # unbracketed in the relevant compilation unit (rare).
        pat_loose = re.compile(
            r"([\w\s\*\(\)]+?)\b" + re.escape(name) +
            r"\s*\(\s*([^{};]*?)\s*\)\s*[{;]",
            re.MULTILINE | re.DOTALL,
        )
        m = pat_loose.search(text)
        if not m:
            return None

    return_type = m.group(1).strip()
    # Detect storage class before cleaning the leading keywords.
    is_static = bool(re.search(r"\bstatic\b", return_type))
    # Clean leading keywords.
    for kw in ["static", "inline", "extern", "__always_inline",
               "noinline", "__init", "__exit"]:
        return_type = re.sub(r"\b" + kw + r"\b", "", return_type)
    return_type = " ".join(return_type.split())

    params_text = m.group(2).strip()
    if not params_text or params_text == "void":
        return Signature(
            return_type=return_type, params=[], is_static=is_static,
        )

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

    return Signature(
        return_type=return_type, params=params, is_static=is_static,
    )


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

    # If the target is static in its TU, goto-cc's
    # --export-file-local-symbols pass mangles its symbol to
    # __CPROVER_file_local_<stem>_c_<name> at link time.  The
    # harness lives in a separate TU, so a call to the
    # unmangled name resolves to a body-less external symbol
    # and CBMC reports "no body for callee" — a spurious
    # FAILURE that would pollute every per-file scan over
    # static kernel helpers.  Use the mangled name in the
    # harness's forward declaration and call site so the link
    # resolves to the real body.
    if sig.is_static:
        callee = (
            f"__CPROVER_file_local_{source.stem}_c_{function}"
        )
    else:
        callee = function

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
    if cfg.get("ghost_init_decl"):
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
        # Kernel-specific typedefs from <linux/types.h> /
        # <linux/posix_types.h> / <asm/posix_types_*.h> that
        # appear pervasively in kernel function signatures.
        # Listing them here keeps the harness from emitting
        # `typedef char loff_t;` (and similar), which conflicts
        # with the kernel TU's existing definition.
        "loff_t", "off_t", "fmode_t", "umode_t", "blkcnt_t",
        "sector_t", "dev_t", "ino_t", "uid_t", "gid_t",
        "pid_t", "time_t", "time64_t", "ktime_t",
        "u8", "u16", "u32", "u64",
        "s8", "s16", "s32", "s64",
        "__u8", "__u16", "__u32", "__u64",
        "__s8", "__s16", "__s32", "__s64",
        "__be16", "__be32", "__be64",
        "__le16", "__le32", "__le64",
        "phys_addr_t", "resource_size_t", "dma_addr_t",
        "gfp_t", "fl_owner_t", "vm_fault_t",
        "atomic_t", "atomic64_t", "atomic_long_t",
        "irqreturn_t", "cycles_t", "clockid_t",
        "qid_t", "key_t", "key_serial_t", "kuid_t", "kgid_t",
        "mode_t", "rwf_t", "spinlock_t", "rwlock_t",
        "seqlock_t", "seqcount_t",
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

    # Target function declaration.  Use the mangled name when
    # the target is static (see comment in `synthesise` above).
    arg_sig = ", ".join(f"{p.type_text} {p.name}" for p in sig.params) \
        or "void"
    lines.append(f"{sig.return_type} {callee}({arg_sig});")
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
    bootstrapped_any = False
    # Track which kernel includes the wrapper-paths logic
    # introduces; we'll insert them into the harness preamble
    # ahead of the harness body so they appear before any code
    # that uses the wrapper struct's layout.
    wrapper_includes: list[str] = []
    for i, p in enumerate(sig.params):
        local = f"arg{i}"
        if "*" in p.type_text:
            lines.append(f"  static char {local}_backing[1024];")
            lines.append(f"  {p.type_text} {local} = "
                         f"({p.type_text}){local}_backing;")
            if is_ghost_tracked(module, p.type_text):
                bootstrapped_any = True
                if cfg.get("custom_setup"):
                    # Multi-statement setup block (e.g. aead's
                    # SGL fabrication).  The template is emitted
                    # verbatim with {i} and {arg} substituted.
                    setup = cfg["custom_setup_template"].format(
                        i=i, arg=local,
                    )
                    for setup_line in setup.splitlines():
                        lines.append(setup_line)
                else:
                    ghost_args = cfg["ghost_init_args_template"].format(
                        arg=local,
                    )
                    lines.append(
                        f"  {cfg['ghost_init_call']}({ghost_args});"
                    )
            else:
                # Wrapper-path bootstrap: even if the parameter
                # type doesn't directly match the bug-class
                # primitive (e.g. nlmclnt_release_host takes
                # `struct nlm_host *`, not `struct cred *`), we
                # may know that the wrapper struct contains a
                # field of the bug-class type.  Bootstrap that
                # field if so.  See cfg['wrapper_paths'] for
                # the (param_type, field_path) pairs.
                for wp in cfg.get("wrapper_paths", []):
                    if wp["param_type"] in p.type_text:
                        for inc in wp.get("kernel_includes", []):
                            if inc not in wrapper_includes:
                                wrapper_includes.append(inc)
                        # field_path is either a bare field
                        # name like "h_cred" (cred_lifetime
                        # picks up `arg->h_cred`) or an
                        # explicit format string with {arg}
                        # placeholder for non-cred paths
                        # (e.g. lock_state's "&{arg}->mutex").
                        path = wp["field_path"]
                        if "{arg}" in path:
                            field_expr = path.format(arg=local)
                        else:
                            field_expr = f"{local}->{path}"
                        bootstrapped_any = True
                        if cfg.get("custom_setup"):
                            # Custom setup with wrapper path:
                            # treat field_expr as the {arg}
                            # substitution.
                            setup = cfg["custom_setup_template"].format(
                                i=i, arg=field_expr,
                            )
                            for setup_line in setup.splitlines():
                                lines.append(setup_line)
                        else:
                            ghost_args = cfg[
                                "ghost_init_args_template"
                            ].format(arg=field_expr)
                            lines.append(
                                f"  {cfg['ghost_init_call']}({ghost_args});"
                            )
                        break  # one wrapper path per parameter
            call_args.append(local)
        else:
            # Scalar: declared-uninitialised ≡ nondet under CBMC.
            lines.append(f"  {p.type_text} {local};")
            call_args.append(local)

    # Insert wrapper-path kernel includes into the harness
    # preamble.  We placed everything after the typedef-fallback
    # block, so insert before the int main() declaration.  Find
    # the first line that begins the entry function and insert
    # the includes immediately before it.
    if wrapper_includes:
        insert_at = None
        for idx, line in enumerate(lines):
            if line.startswith(f"int {harness_name}("):
                insert_at = idx
                break
        if insert_at is not None:
            include_lines = [
                f"#include {inc}" for inc in wrapper_includes
            ] + [""]
            lines = lines[:insert_at] + include_lines + lines[insert_at:]
        else:
            for inc in wrapper_includes:
                lines.insert(0, f"#include {inc}")

    lines.append("")
    if sig.return_type == "void":
        lines.append(f"  {callee}({', '.join(call_args)});")
    else:
        lines.append(
            f"  {sig.return_type} _ret = {callee}({', '.join(call_args)});"
        )
        lines.append("  (void)_ret;")
    lines.append("")
    lines.append("  return 0;")
    lines.append("}")
    lines.append("")

    # Emit a trivial main() that dispatches to the per-file
    # harness entry.  goto-instrument's --aggressive-slice
    # (applied after the contract replacement in
    # scan/scan-per-file.sh) anchors on the linked binary's
    # `main` symbol to identify reachable code.  Without this
    # wrapper, the slice step reports `entry point not found`
    # and is silently skipped — which leaves the scan exposed
    # to large-function timeouts on the per-file budget.
    # cbmc itself invokes the `<function>_per_file_harness`
    # symbol directly via --function and doesn't care which
    # wrapper is the "main" in the binary.
    lines.append("int main(void)")
    lines.append("{")
    lines.append(f"  return {harness_name}();")
    lines.append("}")
    lines.append("")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines))

    print(f"Wrote {out} (harness entry: {harness_name})")
    if warnings:
        for w in warnings:
            print(f"  warning: {w}", file=sys.stderr)
    if not bootstrapped_any:
        # Surface the empty-ghost case to the caller via a
        # marker in the print stream.  scan-per-file.sh
        # propagates this into the verdict notes so corpus-scan
        # can segment "FAILED-with-bootstrap" (high-confidence
        # candidate) from "FAILED-empty-bootstrap" (lower
        # confidence — the contract precondition fires by
        # default on empty-ghost lookups).  Genuine bug-class
        # patterns can still be caught when no parameter type
        # matches: e.g. nfsd_setuser(struct svc_rqst *) has no
        # cred parameter but its body's back-to-back put_cred
        # pattern is real signal.  We do NOT reclassify the
        # verdict.
        print(
            f"  no parameter matched {module}'s ghost-bootstrap "
            "config; harness ghost is empty (lower-confidence "
            "verdict)",
            file=sys.stderr,
        )
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
