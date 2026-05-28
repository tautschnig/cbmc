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
        "put_apis": ["put_cred", "abort_creds"],
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
            {
                # copy_creds(struct task_struct *) and similar
                # task-lifecycle helpers take the wrapper and
                # operate on its cred / real_cred pointers.
                # task->cred is itself a (const struct cred *),
                # so field_path is just "cred" (no leading &).
                "param_type": "struct task_struct *",
                "field_path": "cred",
                "kernel_includes": ["<linux/sched.h>"],
            },
            {
                # exit_creds(struct task_struct *) calls put_cred
                # on BOTH task->real_cred (line ~169) and
                # task->cred (line ~175).  task->cred is bootstrapped
                # by the entry above; bootstrap real_cred too so
                # the L169 hit gets a live ghost.
                "param_type": "struct task_struct *",
                "field_path": "real_cred",
                "kernel_includes": ["<linux/sched.h>"],
            },
            {
                # put_fs_context(struct fs_context *) calls
                # put_cred(fc->cred); bootstrap fc->cred.
                "param_type": "struct fs_context *",
                "field_path": "cred",
                "kernel_includes": ["<linux/fs_context.h>"],
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
        "put_apis": ["mutex_unlock"],
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
        "put_apis": ["refcount_dec_and_test", "__refcount_dec_and_test"],
        "wrapper_paths": [
            {
                # nlm_release_host etc. take the wrapper and
                # operate on its h_count refcount.
                "param_type": "struct nlm_host *",
                "field_path": "&{arg}->h_count",
                "kernel_includes": ["<linux/lockd/lockd.h>"],
            },
            {
                # put_task_struct, put_task_stack etc. take the
                # task wrapper and operate on its embedded
                # refcounts.  Use task->usage as the canonical
                # task refcount.
                "param_type": "struct task_struct *",
                "field_path": "&{arg}->usage",
                "kernel_includes": ["<linux/sched.h>"],
            },
            {
                # __cleanup_sighand(struct sighand_struct *)
                # decrements sighand->count.  Bootstrap it.
                "param_type": "struct sighand_struct *",
                "field_path": "&{arg}->count",
                "kernel_includes": ["<linux/sched/signal.h>"],
            },
            {
                # put_signal_struct(struct signal_struct *)
                # decrements signal->sigcnt.  Bootstrap it.
                "param_type": "struct signal_struct *",
                "field_path": "&{arg}->sigcnt",
                "kernel_includes": ["<linux/sched/signal.h>"],
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
    "kobject_lifetime": {
        # Match struct kobject *-typed parameters; per-file
        # harness inits each with usage=1 so the first put lands
        # on a live kobject and a double-put / unbalanced-put
        # pattern inside the enclosing function fires the
        # contract precondition.
        "types": ["struct kobject *"],
        "ghost_init_call": "kobject_lifetime_init",
        "ghost_init_args_template": "(struct kobject *){arg}, 1",
        "ghost_init_decl":
            "void kobject_lifetime_init(struct kobject *k, "
            "unsigned int usage);",
        "forward_decls": ["struct kobject;"],
        "put_apis": ["kobject_put"],
        # Wrapper-paths can be added here once cross-version
        # corpus runs surface concrete wrapper struct → kobject
        # patterns (e.g. struct device, struct net_device).
        # Empty for now; the kobject types hit applies to bare
        # struct kobject * params first.
        "wrapper_paths": [],
    },
    # === Phase-1 balance modules generated via
    #     scan/balance_module_factory.py.  Each follows the
    #     cred/refcount/kobject pattern: ghost-init at usage=1,
    #     contract on the kernel's put-API.
    "device_lifetime": {
        "types": ["struct device *"],
        "ghost_init_call": "device_lifetime_init",
        "ghost_init_args_template": "(struct device *){arg}, 1",
        "ghost_init_decl":
            "void device_lifetime_init(struct device *dev, "
            "unsigned int usage);",
        "forward_decls": ["struct device;"],
        # Put-style API call sites that the synthesiser can
        # use for auto-discovery of wrapper paths.  When a
        # function body contains `put_device(EXPR)` where
        # EXPR is `param->field`, the synthesiser auto-adds
        # a wrapper_path bootstrap for that field.
        "put_apis": ["put_device"],
        "wrapper_paths": [
            {
                # attribute_container_release(struct device *classdev)
                # and similar driver-core release paths put the
                # parent device.  classdev->parent is a
                # `struct device *` reachable via wrapper_paths.
                # Bootstrapping it lifts the corresponding
                # corpus row out of low-confidence.
                "param_type": "struct device *",
                "field_path": "parent",
                "kernel_includes": ["<linux/device.h>"],
            },
        ],
    },
    "of_node_lifetime": {
        "types": ["struct device_node *"],
        "ghost_init_call": "of_node_lifetime_init",
        "ghost_init_args_template": "(struct device_node *){arg}, 1",
        "ghost_init_decl":
            "void of_node_lifetime_init(struct device_node *node, "
            "unsigned int usage);",
        "forward_decls": ["struct device_node;"],
        "put_apis": ["of_node_put"],
        "wrapper_paths": [],
    },
    "inode_lifetime": {
        "types": ["struct inode *"],
        "ghost_init_call": "inode_lifetime_init",
        "ghost_init_args_template": "(struct inode *){arg}, 1",
        "ghost_init_decl":
            "void inode_lifetime_init(struct inode *inode, "
            "unsigned int usage);",
        "forward_decls": ["struct inode;"],
        "put_apis": ["iput"],
        "wrapper_paths": [
            {
                # ext2_link, vfs_link, and many fs/ helpers extract
                # an inode via d_inode(old_dentry) and call iput.
                # Bootstrapping dentry->d_inode resolves the
                # corresponding low-confidence rows.
                "param_type": "struct dentry *",
                "field_path": "d_inode",
                "kernel_includes": ["<linux/dcache.h>"],
            },
        ],
    },
    "dentry_lifetime": {
        "types": ["struct dentry *"],
        "ghost_init_call": "dentry_lifetime_init",
        "ghost_init_args_template": "(struct dentry *){arg}, 1",
        "ghost_init_decl":
            "void dentry_lifetime_init(struct dentry *dentry, "
            "unsigned int usage);",
        "forward_decls": ["struct dentry;"],
        "put_apis": ["dput"],
        "wrapper_paths": [
            {
                # generic_shutdown_super and similar superblock-
                # teardown helpers put sb->s_root.
                "param_type": "struct super_block *",
                "field_path": "s_root",
                "kernel_includes": ["<linux/fs.h>"],
            },
        ],
    },
    "fput_lifetime": {
        "types": ["struct file *"],
        "ghost_init_call": "fput_lifetime_init",
        "ghost_init_args_template": "(struct file *){arg}, 1",
        "ghost_init_decl":
            "void fput_lifetime_init(struct file *file, "
            "unsigned int usage);",
        "forward_decls": ["struct file;"],
        "put_apis": ["fput"],
        "wrapper_paths": [],
    },
    "sock_lifetime": {
        "types": ["struct sock *"],
        "ghost_init_call": "sock_lifetime_init",
        "ghost_init_args_template": "(struct sock *){arg}, 1",
        "ghost_init_decl":
            "void sock_lifetime_init(struct sock *sk, "
            "unsigned int usage);",
        "forward_decls": ["struct sock;"],
        "put_apis": ["sock_put"],
        "wrapper_paths": [],
    },
    "skb_lifetime": {
        "types": ["struct sk_buff *"],
        "ghost_init_call": "skb_lifetime_init",
        "ghost_init_args_template": "(struct sk_buff *){arg}, 1",
        "ghost_init_decl":
            "void skb_lifetime_init(struct sk_buff *skb, "
            "unsigned int usage);",
        "forward_decls": ["struct sk_buff;"],
        "put_apis": ["kfree_skb"],
        "wrapper_paths": [],
    },
    "module_lifetime": {
        "types": ["struct module *"],
        "ghost_init_call": "module_lifetime_init",
        "ghost_init_args_template": "(struct module *){arg}, 1",
        "ghost_init_decl":
            "void module_lifetime_init(struct module *module, "
            "unsigned int usage);",
        "forward_decls": ["struct module;"],
        "put_apis": ["module_put"],
        "wrapper_paths": [],
    },
    "kref_lifetime": {
        "types": ["struct kref *"],
        "ghost_init_call": "kref_lifetime_init",
        "ghost_init_args_template": "(struct kref *){arg}, 1",
        "ghost_init_decl":
            "void kref_lifetime_init(struct kref *kref, "
            "unsigned int usage);",
        "forward_decls": ["struct kref;"],
        "put_apis": ["kref_put"],
        "wrapper_paths": [],
    },
    "netlink_attr_validation": {
        # Per-file harness inits each struct nlattr * parameter
        # with validated_min_size=0 (unvalidated).  Functions
        # that call nla_get_uX without first calling
        # nla_validate_min_size will fire the contract.
        "types": ["struct nlattr *"],
        "ghost_init_call": "nla_validate_min_size",
        # min_size=0 means "not yet validated".  Functions
        # under test must explicitly raise the bound.
        "ghost_init_args_template":
            "(struct nlattr *){arg}, 0",
        "ghost_init_decl":
            "void nla_validate_min_size(struct nlattr *attr, "
            "unsigned int min_size);",
        "forward_decls": ["struct nlattr;"],
        "wrapper_paths": [],
    },
    "resource_leak_on_error_path": {
        # No parameter-typed ghost-init at harness entry —
        # the bug-class semantics live entirely inside the
        # function body (allocation here, missing free on
        # error exit there).  When INSTRUMENT=cocci is
        # active, the cocci-instrumenter inserts
        # leak_alloc_track(x); right after each
        # kmalloc-family allocation and __assert_no_leak_at_exit(x);
        # before each return.  The harness just needs to
        # invoke the kernel function so those instrumented
        # call sites get exercised.
        "types": [],
        "ghost_init_call": None,
        "ghost_init_args_template": None,
        "ghost_init_decl": "",
        "forward_decls": [],
        "wrapper_paths": [],
        # Marker for the synthesizer: empty-ghost is
        # EXPECTED for this module; suppress the
        # low-confidence warning that other modules emit
        # when their ghost-bootstrap is empty.
        "uses_cocci_instrumentation": True,
    },
    "null_after_alloc": {
        # Same shape as resource_leak_on_error_path: the
        # bug class's state is in the function body, not
        # in any parameter.  Cocci-instrumented kernel TUs
        # carry __assert_safe_to_deref(x) checks before
        # each x->field deref.
        "types": [],
        "ghost_init_call": None,
        "ghost_init_args_template": None,
        "ghost_init_decl": "",
        "forward_decls": [],
        "wrapper_paths": [],
        "uses_cocci_instrumentation": True,
    },
    "use_after_free_generic": {
        # Same shape as the other cocci-driven modules:
        # __assert_not_freed(x) is inserted by cocci before
        # each x->field deref reached after kfree(x).
        "types": [],
        "ghost_init_call": None,
        "ghost_init_args_template": None,
        "ghost_init_decl": "",
        "forward_decls": [],
        "wrapper_paths": [],
        "uses_cocci_instrumentation": True,
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
    # Reject matches where the "return type" is empty or
    # doesn't look like a type — those usually came from the
    # loose pattern matching a function CALL like
    # `    foo(arg);` rather than a definition or
    # declaration.  A real return type contains at least one
    # type-like token (a keyword or a struct/typedef name).
    # Empty-string return types or pure punctuation are
    # rejected.
    if not return_type or not re.search(r"[A-Za-z_]", return_type):
        return None
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


def _is_aead_transform_wrapper(source: Path, function: str) -> bool:
    """Return True if ``function`` follows the aead transform-wrapper
    shape that the per-file synthesis fundamentally cannot validate:
    the function takes ``struct aead_request *`` and its body calls
    ``aead_request_set_crypt(X, ...)`` where ``X`` is NOT the
    function's primary parameter name (i.e. it is a freshly
    allocated subrequest carrying its own SGL).

    Functions matching this shape -- e.g. ``crypto_rfc4309_crypt``,
    ``crypto_rfc4106_crypt``, ``crypto_rfc4543_crypt`` -- always
    showed up as 'failed' in the May 2026 hunt, but the failures
    were artefacts: the harness sets ``req->dst`` to a SGL it
    controls, then the function ignores that SGL by switching
    to ``subreq``'s SGL via ``aead_request_set_crypt(subreq, ...)``,
    and the property module's ``sgl_all_user_writable(dst)``
    precondition fires against the unmodelled subreq SGL.
    """
    text = _strip_comments(source.read_text(errors="replace"))
    sig = find_function_signature(source, function)
    if sig is None:
        return False

    # Primary parameter must be a (non-const) pointer to
    # aead_request.  We accept both 'struct aead_request *' and
    # 'aead_request_t *' just in case.
    if not sig.params:
        return False
    p0 = sig.params[0]
    if "aead_request" not in p0.type_text:
        return False
    primary = p0.name

    # Find the function body.  find_function_signature returns the
    # signature; we re-use a tighter regex to extract the body
    # block.
    pat = re.compile(
        r"(?:^|[;}\n])\s*[\w\s\*\(\)]+?\b" + re.escape(function) +
        r"\s*\(\s*[^{};]*?\s*\)\s*\{",
        re.MULTILINE | re.DOTALL,
    )
    m = pat.search(text)
    if not m:
        return False

    # Walk braces to find the end of the function body.
    body_start = m.end()
    depth = 1
    i = body_start
    while i < len(text) and depth > 0:
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1
    body = text[body_start:i - 1] if depth == 0 else text[body_start:]

    # Look for aead_request_set_crypt(X, ...) where X != primary.
    for call_m in re.finditer(
        r"aead_request_set_crypt\s*\(\s*([A-Za-z_]\w*)",
        body,
    ):
        first_arg = call_m.group(1)
        if first_arg != primary:
            return True

    return False


def _uses_current_macro(source: Path, function: str) -> bool:
    """Return True if ``function``'s body reads ``current->...`` —
    i.e. dereferences the per-CPU "currently running task" pointer.

    The kernel's ``current`` macro expands via ``<asm/current.h>``
    to ``get_current()``, which reads the per-CPU variable
    ``current_task``.  CBMC has no model for per-CPU storage:
    symbolic execution sees an unconstrained pointer (typically
    NULL after zero-initialisation), so any contract precondition
    on a value derived from ``current`` (e.g. ``current->cred``
    in ``revert_creds``) fires spuriously.

    Modelling ``current`` properly would require either
    overriding ``<asm/current.h>`` on every TU's compile (which
    creates struct-completeness link conflicts between the
    harness and the kernel TU) or a goto-binary post-processing
    pass on ``get_current``'s body.  Both are non-trivial.

    A conservative middle path: if the function's body textually
    references ``current->`` or passes ``current`` directly to
    another function, mark it as a known-unverifiable shape and
    skip with ``status=skipped``.  This removes a known class of
    false positive (the second-followup triage's "fundamental
    limitation" bucket) from the per-file rollup.
    """
    text = _strip_comments(source.read_text(errors="replace"))
    sig = find_function_signature(source, function)
    if sig is None:
        return False

    # Find function body start: '{' after the signature.
    pat = re.compile(
        r"\b" + re.escape(function) + r"\s*\([^)]*\)\s*\{",
        re.DOTALL,
    )
    m = pat.search(text)
    if not m:
        return False
    body_start = m.end()
    depth = 1
    i = body_start
    while i < len(text) and depth > 0:
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1
    body = text[body_start:i - 1] if depth == 0 else text[body_start:]

    # `current->` (field access) or `current,` / `current)`
    # (passed to another function) are the high-precision
    # signals.  Avoid matching e.g. `current_task` or
    # `current_cred` (those don't go through the macro).
    if re.search(r"\bcurrent\s*->", body):
        return True
    if re.search(r"\bcurrent\s*,", body):
        return True
    if re.search(r"\bcurrent\s*\)", body):
        return True
    return False


def _autodiscover_wrapper_paths(cfg: dict, source: Path,
                                function: str,
                                params: list) -> list[dict]:
    """Scan the function body for `put_api(EXPR)` calls and
    auto-discover wrapper-paths from EXPR shapes like
    `param->field` or `&param->field`.

    Returns NEW wrapper-path dicts to extend cfg["wrapper_paths"]
    with.  Skips entries that duplicate an existing wrapper_path.

    For the field access in the harness to type-check, the
    parameter's struct layout must be visible.  We try to find
    a likely header by scanning the source file's #include
    lines for one that mentions the struct tag in its name.
    If we can't, we skip auto-discovery for that param (rather
    than emit a harness that won't compile).
    """
    put_apis = cfg.get("put_apis") or []
    if not put_apis:
        return []
    try:
        body = source.read_text(errors="replace")
    except OSError:
        return []
    # Collect all #include <linux/...> lines from the source.
    src_includes = re.findall(
        r'^\s*#\s*include\s+(<[^>]+>)', body, re.MULTILINE)
    # Find the function body.
    pat = re.compile(r"\b" + re.escape(function) + r"\s*\(")
    fn_body: str | None = None
    for m in pat.finditer(body):
        depth = 0
        i = m.end() - 1
        while i < len(body):
            c = body[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    j = i + 1
                    break
            i += 1
        else:
            continue
        while j < len(body) and body[j] in " \t\n\r":
            j += 1
        if j >= len(body) or body[j] != "{":
            continue
        bdepth = 1
        end = j + 1
        while end < len(body) and bdepth > 0:
            c = body[end]
            if c == "{":
                bdepth += 1
            elif c == "}":
                bdepth -= 1
            end += 1
        fn_body = body[j:end]
        break
    if fn_body is None:
        return []
    param_type: dict[str, str] = {p.name: p.type_text for p in params}

    def _guess_header(struct_tag: str) -> list[str]:
        """Pick #include lines from the source whose stem
        (filename without extension or directory) is an
        EXACT match for the struct tag or its first/last
        underscore-separated component.

        Strict matching avoids false-positive picks like
        `<drm/lima_drm.h>` for `struct lima_bo` (which is
        actually defined in a private driver header).  When
        no strict match is found we return [] and the caller
        skips auto-discovery for that param — preferring an
        empty-ghost successful verdict over an
        uncompilable harness."""
        candidates: list[str] = []
        tag_lower = struct_tag.lower()
        parts = tag_lower.split("_")
        tokens: list[str] = [tag_lower]
        if len(parts) > 1:
            tokens.append(parts[0])
            tokens.append(parts[-1])
        for inc in src_includes:
            stem = inc.strip("<>").rsplit("/", 1)[-1]
            stem_no_ext = stem.split(".", 1)[0].lower()
            for tok in tokens:
                if tok and tok == stem_no_ext:
                    if inc not in candidates:
                        candidates.append(inc)
                    break
        return candidates

    discovered: dict[tuple[str, str], dict] = {}
    existing = {(wp["param_type"], wp["field_path"])
                for wp in cfg.get("wrapper_paths", [])}
    for api in put_apis:
        api_pat = re.compile(
            r"\b" + re.escape(api) + r"\s*\(\s*"
            r"(&\s*)?([A-Za-z_]\w*)\s*->\s*(\w+)\b"
        )
        for m in api_pat.finditer(fn_body):
            amp, var, fld = m.group(1), m.group(2), m.group(3)
            if var not in param_type:
                continue
            ptype = param_type[var]
            field_path = (f"&{{arg}}->{fld}" if amp else fld)
            key = (ptype, field_path)
            if key in existing or key in discovered:
                continue
            # Identify the struct tag from the param type.
            tag_m = re.search(r"struct\s+(\w+)", ptype)
            if not tag_m:
                continue
            tag = tag_m.group(1)
            includes = _guess_header(tag)
            if not includes:
                # Couldn't find a header that declares this
                # struct; skip — emitting a wrapper-path that
                # accesses an opaque struct's fields would
                # produce an uncompilable harness.
                continue
            discovered[key] = {
                "param_type": ptype,
                "field_path": field_path,
                "kernel_includes": includes,
                "auto_discovered": True,
            }
    return list(discovered.values())


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
        # Exit 5: function not found in this TU.  Different from
        # exit 2 (real synth failure) so scan-per-file.sh can
        # surface this as a "skipped" verdict rather than an
        # error — we can't verify what isn't there.
        return 5

    # Auto-discover wrapper-paths from put-API call sites in
    # the function body.  This addresses the empty-ghost
    # false-negative case: when the kernel function takes
    # `struct most_interface *iface` and calls
    # `put_device(iface->dev)`, the harness can't bootstrap
    # the right ghost without knowing iface->dev is the
    # tracked pointer.  Auto-discovery synthesises that
    # wrapper path from the function body directly.
    auto_paths = _autodiscover_wrapper_paths(
        cfg, source, function, sig.params)
    if auto_paths:
        # Extend cfg with a copy that includes auto-paths.
        cfg = dict(cfg)
        cfg["wrapper_paths"] = list(cfg.get("wrapper_paths", [])) \
            + auto_paths

    # Skip the aead transform-wrapper shape: per-file synthesis
    # has no way to model the freshly-allocated subreq's SGL
    # so it always produces a spurious 'failed' verdict.  Signal
    # the caller via exit code 4 (defined in scan-per-file.sh as
    # 'skip-known-wrapper-pattern').
    if module == "aead" and _is_aead_transform_wrapper(source, function):
        print(
            f"synthesise_harness: {function} matches the aead "
            "transform-wrapper shape (allocates a fresh subreq and "
            "calls aead_request_set_crypt(subreq, ...)); per-file "
            "synthesis cannot validate this without modelling the "
            "subreq's SGL.  Skipping.",
            file=sys.stderr,
        )
        return 4

    # Skip functions whose body reads `current` (the per-CPU
    # current-task pointer).  CBMC has no model for per-CPU
    # storage so values derived from `current` are unconstrained;
    # any contract precondition on `current->cred` /
    # `current->mm` fires spuriously.  See _uses_current_macro for
    # rationale.  Same exit code 4 routing as the aead detector.
    if module in ("cred_lifetime", "lock_state", "refcount_lifetime") \
            and _uses_current_macro(source, function):
        print(
            f"synthesise_harness: {function} reads `current` "
            "(per-CPU current-task pointer); CBMC has no model "
            "for per-CPU storage so any contract precondition on "
            "values derived from `current` fires spuriously.  "
            "Skipping.",
            file=sys.stderr,
        )
        return 4

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
    #
    # NOTE: goto-cc replaces non-identifier characters (dashes,
    # dots, etc.) in the stem with underscores so the mangled
    # name is a legal C identifier.  Mirror that mapping here
    # so files like `acp-es8336.c` yield
    # `__CPROVER_file_local_acp_es8336_c_<name>`.
    if sig.is_static:
        sanitised_stem = re.sub(r"[^A-Za-z0-9_]", "_", source.stem)
        callee = (
            f"__CPROVER_file_local_{sanitised_stem}_c_{function}"
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
            # Use a stack-local (non-static) buffer so CBMC
            # treats its contents as nondet.  The kernel
            # function under test will then explore both
            # success and error paths through field reads.
            # (Static buffers are zero-initialised in C, so
            # CBMC would only see all-zero inputs and miss
            # the alloc-success path.)
            lines.append(f"  char {local}_backing[1024];")
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

            # Wrapper-path bootstrap.  Apply EVEN IF the param
            # type already matched the bug-class type — many
            # functions on `struct device *classdev` also
            # dereference `classdev->parent`, etc.  Each matching
            # wrapper_path adds an additional ghost-init.
            #
            # For pointer fields (bare-name paths that don't
            # already start with `&`), zero-init makes the field
            # read NULL — which fails the `!= NULL` precondition
            # before our ghost lookup runs.  Auto-assign a fresh
            # static backing buffer so the field is non-NULL and
            # the contract precondition can succeed when the
            # ghost is live.
            for wp_idx, wp in enumerate(cfg.get("wrapper_paths", [])):
                if wp["param_type"] not in p.type_text:
                    continue
                for inc in wp.get("kernel_includes", []):
                    if inc not in wrapper_includes:
                        wrapper_includes.append(inc)
                path = wp["field_path"]
                # Detect "address of embedded field" vs
                # "pointer to other struct" via the path string.
                # `&{arg}->...` or `&...` paths mean we're
                # taking the address of an embedded field;
                # bare names like "parent" or "d_inode" mean
                # we're reading a pointer field whose value
                # was zero-init'd to NULL.
                takes_address = path.startswith("&") or (
                    "{arg}" in path and path.lstrip().startswith("&")
                )
                if "{arg}" in path:
                    field_expr = path.format(arg=local)
                else:
                    field_expr = f"{local}->{path}"

                if not takes_address:
                    # Pointer-field path: assign a backing
                    # buffer so the field is non-NULL.  Use a
                    # per-(param, wp_idx) backing to avoid name
                    # collisions when a param has multiple
                    # wrapper paths.
                    backing_name = f"{local}_wp{wp_idx}_backing"
                    lines.append(
                        f"  static char {backing_name}[1024];"
                    )
                    # Cast through `void *` to avoid
                    # const-violation warnings when the field is
                    # `const struct X *`.
                    lines.append(
                        f"  *(void **)&{field_expr} = "
                        f"(void *){backing_name};"
                    )

                bootstrapped_any = True
                if cfg.get("custom_setup"):
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
        if cfg.get("uses_cocci_instrumentation"):
            # This module's ghost state is bootstrapped by
            # cocci-inserted calls in the kernel TU, not by
            # parameter-based init at harness entry.  Empty
            # ghost is the expected state of the harness
            # itself; the verdict is full confidence as long
            # as the cocci instrumentation actually fired
            # (caller signals this via INSTRUMENT=...).
            print(
                f"  module {module} relies on cocci-inserted "
                "ghost bootstrap inside the kernel TU "
                "(INSTRUMENT=... required for meaningful "
                "verdict)",
                file=sys.stderr,
            )
        else:
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
