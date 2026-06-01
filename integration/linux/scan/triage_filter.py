"""triage_filter.py — Heuristic postfilter for per-file
verdicts.

Identifies two well-understood false-positive shapes that the
ghost-only per-function harness produces for refcount-balance
modules:

1. **escape_via_store** — the function allocates X (or
   takes a get on X) and writes X into a struct field that
   the caller owns.  The caller is responsible for the
   matching put; the ghost-only harness cannot see this.

2. **put_only_on_error** — the function allocates X and
   only calls put(X) inside cleanup blocks reached via an
   error-path goto or `if (err)`.  No double-put bug; the
   put fires only when allocation succeeded but a later
   step failed.

The filter operates on the kernel function's source text
using simple regex patterns; it intentionally errs toward
under-filtering (false negatives in the filter == real
candidate kept for review).
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


# ---------------------------------------------------------------------------
# Function-body extraction
# ---------------------------------------------------------------------------

def _function_body(source: str, fn_name: str) -> str | None:
    """Extract the body of `fn_name` from `source`.

    Uses a simple brace-matching parser on the first match
    of `fn_name(` at column 0 or following a return type.
    Returns None if not found.
    """
    # Match a function signature:  TYPE? (... )?  fn_name ( ... ) {
    # We look for `\bfn_name\s*\(` and walk back to find a
    # likely signature start, then forward to find the
    # opening brace of the body.
    pat = re.compile(r"\b" + re.escape(fn_name) + r"\s*\(", re.MULTILINE)
    for m in pat.finditer(source):
        # Heuristic: the function definition (not a call) is
        # one where the closing ')' is followed by an
        # optional attribute(s) and then a '{'.  Walk forward.
        depth = 0
        i = m.end() - 1  # at the '('
        while i < len(source):
            c = source[i]
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
        # Skip whitespace, attributes, until `{` or `;`.
        while j < len(source) and source[j] in " \t\n\r":
            j += 1
        # Possible attribute lists; eat them.
        while j < len(source) and source[j:j+2] == "__":
            # __attribute__((...)) etc.  Skip identifier and
            # any parenthesised arg.
            k = j
            while k < len(source) and (source[k].isalnum() or source[k] == "_"):
                k += 1
            while k < len(source) and source[k] in " \t\n\r":
                k += 1
            if k < len(source) and source[k] == "(":
                pdepth = 0
                while k < len(source):
                    if source[k] == "(":
                        pdepth += 1
                    elif source[k] == ")":
                        pdepth -= 1
                        if pdepth == 0:
                            k += 1
                            break
                    k += 1
            j = k
            while j < len(source) and source[j] in " \t\n\r":
                j += 1
        if j >= len(source) or source[j] != "{":
            # Not a definition (probably a call or a decl).
            continue
        # Walk braces.
        bdepth = 1
        end = j + 1
        while end < len(source) and bdepth > 0:
            c = source[end]
            if c == "{":
                bdepth += 1
            elif c == "}":
                bdepth -= 1
            end += 1
        if bdepth == 0:
            return source[j:end]
    return None


# ---------------------------------------------------------------------------
# Shape detectors
# ---------------------------------------------------------------------------

# Common allocation/get APIs and the matching put APIs.
#
# We split into FRESH_ALLOC_APIS (return a brand-new object,
# initial refcount 1) and REFCOUNT_GET_APIS (increment a
# refcount on an existing object).  Several detectors only
# apply to the FRESH set: a `var = dget(x); dget_parent(var);
# dput(var)` pattern is a perfectly balanced refcount, NOT
# an ownership transfer to dget_parent — the alloc_handed_to_
# consumer detector should not fire.  We also keep the union
# `ALLOC_APIS` as the broad regex for general candidate
# enumeration.
FRESH_ALLOC_APIS = (
    r"kmalloc|kzalloc|kcalloc|kmalloc_array|"
    r"kvmalloc|kvzalloc|kvcalloc|kvmalloc_array|"
    r"kstrdup|kstrndup|kmemdup|kmemdup_nul|kasprintf|kvasprintf|"
    r"kmem_cache_alloc|kmem_cache_zalloc|"
    r"alloc_skb|dev_alloc_skb|nlmsg_new|genlmsg_new|"
    r"usb_alloc_urb|"
    r"kobject_create_and_add|prepare_kernel_cred|prepare_creds|"
    r"d_alloc|new_inode"
)
REFCOUNT_GET_APIS = (
    r"override_creds|get_cred|get_task_cred|"
    r"kobject_get|of_node_get|get_device|igrab|dget|fget|"
    r"sock_hold|skb_get|kref_get|try_module_get|get_file"
)
ALLOC_APIS = FRESH_ALLOC_APIS + r"|" + REFCOUNT_GET_APIS

PUT_APIS = (
    r"kfree|kvfree|kfree_skb|kmem_cache_free|kobject_put|"
    r"put_cred|abort_creds|"
    r"of_node_put|put_device|iput|dput|fput|sock_put|"
    r"kref_put|module_put|kobject_del"
)

ALLOC_RE = re.compile(
    r"(\b(?:[A-Za-z_][A-Za-z_0-9]*)\b)\s*=\s*(?:" + ALLOC_APIS + r")\s*\(",
)
PUT_RE = re.compile(
    r"(?:" + PUT_APIS + r")\s*\(\s*([A-Za-z_][A-Za-z_0-9]*)\s*\)",
)


@dataclass
class FilterVerdict:
    shape: str | None         # "escape_via_store" | "put_only_on_error" | None
    reason: str               # human-readable explanation
    confidence: str           # "high" | "medium" | "low"


def _detect_escape_via_store(body: str, var: str) -> FilterVerdict | None:
    """Return a verdict if `var` escapes via store-to-struct
    or by being returned."""
    # Pattern 1: `xx->field = var;`  (write to caller-owned struct)
    p1 = re.compile(
        r"\b[A-Za-z_][A-Za-z_0-9]*\s*->\s*[A-Za-z_][A-Za-z_0-9]*"
        r"\s*=\s*" + re.escape(var) + r"\s*;",
    )
    # Pattern 2: `*ptr = var;`  (write through caller-provided ptr)
    p2 = re.compile(
        r"\*\s*[A-Za-z_][A-Za-z_0-9]*\s*=\s*" + re.escape(var) + r"\s*;",
    )
    # Pattern 3: `return var;`  (caller takes ownership)
    p3 = re.compile(r"\breturn\s+" + re.escape(var) + r"\s*;")
    # Pattern 4: list_add(&var->...) etc. — implicit publish.
    p4 = re.compile(
        r"\b(?:list_add(?:_tail)?|hlist_add_head|llist_add|"
        r"rb_link_node|atomic_add)\s*\([^;]*\b"
        + re.escape(var) + r"\b",
    )
    if p1.search(body):
        return FilterVerdict(
            "escape_via_store",
            f"`{var}` is written to a struct field "
            "(ownership transfer)",
            "high",
        )
    if p2.search(body):
        return FilterVerdict(
            "escape_via_store",
            f"`{var}` is written through a caller-provided "
            "pointer (output param)",
            "high",
        )
    if p3.search(body):
        return FilterVerdict(
            "escape_via_store",
            f"`{var}` is returned to the caller "
            "(ownership transfer)",
            "high",
        )
    if p4.search(body):
        return FilterVerdict(
            "escape_via_store",
            f"`{var}` is published into a list/tree "
            "(implicit ownership)",
            "medium",
        )
    return None


def _detect_put_only_on_error(body: str, var: str) -> FilterVerdict | None:
    """Return a verdict if every put(var) is inside an
    error-cleanup region (after a goto err..., inside an
    `if (err)` block, etc.)."""
    put_pat = re.compile(
        r"(?:" + PUT_APIS + r")\s*\(\s*" + re.escape(var) + r"\s*\)",
    )
    matches = list(put_pat.finditer(body))
    if not matches:
        return None
    # For each put match, check whether the preceding ~80
    # chars contain an error label, error goto, or `if
    # (err...|ret...|< 0|IS_ERR)` keyword.
    err_ctx = re.compile(
        r"\b(?:err\w*|fail\w*|error\w*|out_\w+|undo_\w+|"
        r"goto\s+(?:err|out|fail|undo)\w*|"
        r"return\s+(?:-E|ret\b|err\b)|"
        r"if\s*\(\s*(?:err|ret|!\s*\w+|IS_ERR|PTR_ERR|"
        r"\w+\s*<\s*0))",
    )
    all_in_err = True
    for m in matches:
        ctx_start = max(0, m.start() - 200)
        ctx = body[ctx_start:m.start()]
        # Look for the most recent label or goto.
        # If the closest control marker is an error-path
        # marker, count it as error-context.
        if not err_ctx.search(ctx):
            all_in_err = False
            break
    if all_in_err and matches:
        return FilterVerdict(
            "put_only_on_error",
            f"all put(`{var}`) sites are inside error-cleanup "
            f"contexts ({len(matches)} call(s))",
            "medium",
        )
    return None


# Functions whose name itself indicates an ownership-handing
# operation.  When the per-file harness fires a contract on
# such a function, the most likely explanation is that the
# caller has already arranged the lifetime preconditions —
# the harness cannot see this and treats them as
# unsatisfied.
_OWNERSHIP_FN_PREFIXES = (
    "put_", "release_", "free_", "exit_", "destroy_",
    "cleanup_", "clean_", "commit_", "unlink_", "delete_",
    "del_", "abort_", "remove_", "disconnect_",
    "unregister_", "deinit_",
)
_OWNERSHIP_FN_SUFFIXES = (
    "_put", "_release", "_free", "_fini", "_deinit",
    "_exit", "_destroy", "_cleanup", "_clean", "_unlink",
    "_delete", "_del", "_abort", "_dec_and_test", "_remove",
    "_disconnect", "_unregister", "_uninit",
)


def _detect_param_consumed_by_callee(
        body: str, param_names: list[str]) -> FilterVerdict | None:
    """Match the pattern where a pointer-typed parameter is
    passed to another function and never directly
    dereferenced in this function's body.  Common in
    network-stack 'forward' functions:
        int uld_send(struct adapter *adap, struct sk_buff *skb, ...) {
            ...
            return ctrl_xmit(&adap->sge.ctrlq[idx], skb);
        }
    The skb is consumed by ctrl_xmit (or kfree_skb on the
    error path); this function transfers ownership but never
    derefs skb itself.  The use_after_free_generic
    instrumentation may then assert on the next deref site
    in a hypothetical caller and fire falsely.

    Heuristic: a parameter `p` is "consumed" when:
      * `p->fld`, `*p`, and `p[i]` are all absent, AND
      * `p` appears as an argument inside a function call.
    """
    if not param_names:
        return None
    for p in param_names:
        esc = re.escape(p)
        if re.search(r"(?<![\w])" + esc + r"\s*->", body):
            continue
        if re.search(
                r"(?<![\w&])\*\s*" + esc + r"(?![\w])", body):
            continue
        if re.search(r"(?<![\w])" + esc + r"\s*\[", body):
            continue
        passed = re.search(
            r"\b[A-Za-z_]\w*\s*\([^()]*\b" + esc + r"\b[^()]*\)",
            body)
        if passed:
            return FilterVerdict(
                "param_consumed_by_callee",
                f"parameter `{p}` is passed to another "
                "function and never directly dereferenced "
                "here — ownership transfers to the callee",
                "medium",
            )
    return None


def _function_param_names_pointer_typed(
        source: str, fn_name: str) -> list[str]:
    """Return bare parameter names of pointer-typed parameters
    of the given function, parsed from the signature in source."""
    pat = re.compile(
        r"(?:^|\n)\s*"
        r"(?:[\w\s\*\(\)]+?\s+)?"
        + re.escape(fn_name) + r"\s*\("
        r"(?P<params>[^)]*)\)"
    )
    m = pat.search(source)
    if not m:
        return []
    params = m.group("params")
    out: list[str] = []
    for p in params.split(","):
        p = p.strip()
        if not p or p == "void":
            continue
        if "*" not in p:
            continue
        name_match = re.search(r"(\w+)\s*(?:\[\s*\])?\s*$", p)
        if name_match:
            out.append(name_match.group(1))
    return out


def _detect_netlink_caller_validated(
        source: str, fn_name: str, body: str
        ) -> FilterVerdict | None:
    """Match netlink reader functions whose attributes were
    validated by the caller (via `nla_parse_nested(...,
    policy)`).  The function is purely a reader: takes an
    `nlattr **` / `nlattr *[]` parameter, calls `nla_get_*`
    on indexed entries (typically guarded by `if (attrs[idx])`),
    does no allocation or freeing of its own.

    Concrete shapes caught:
        cfhsi_netlink_parms(struct nlattr *data[], ...)
        set_allowedip(struct wg_peer *, struct nlattr **attrs)
        ip_tun_from_nlattr(const struct nlattr *attr, ...)
        gtp_find_pdp_by_link(struct net *, struct nlattr *nla[])

    The cocci's per-function netlink_attr_validation
    instrumentation can't see the caller's validation pass;
    these functions fire CONTRACT VIOLATION by default at
    every nla_get_* call.
    """
    # Signature must declare an nlattr parameter.
    sig_re = re.compile(
        r"\b" + re.escape(fn_name) + r"\s*\("
        r"(?P<params>[^)]*)\)"
    )
    sigs = list(sig_re.finditer(source))
    has_nla = False
    for m in sigs:
        params = m.group("params")
        if re.search(r"struct\s+nlattr\s*\*", params):
            has_nla = True
            break
    if not has_nla:
        return None
    # Body must call nla_get_*; that's the actual reader pattern.
    if not re.search(r"\bnla_get_\w+\s*\(", body):
        return None
    # Body must NOT allocate or free anything (this would be
    # a more general function with side-effects).
    if ALLOC_RE.search(body):
        return None
    if PUT_RE.search(body):
        return None
    if re.search(r"\b(?:kfree|kvfree|kfree_skb|nlmsg_free|"
                 r"sk_free)\s*\(", body):
        return None
    return FilterVerdict(
        "netlink_caller_validated",
        "function reads netlink attributes but does not "
        "allocate/free; caller validated via "
        "nla_parse_nested(..., policy) — per-function check "
        "cannot see caller's validation",
        "high",
    )


def _detect_unmatched_unlock(body: str) -> FilterVerdict | None:
    """Match the pattern where the function calls mutex_unlock /
    spin_unlock / read_unlock / write_unlock without a matching
    *_lock for the same lock object inside the body.

    Common shape:

        static int rcar_gen3_phy_usb2_power_off(struct phy *p) {
            ...
            mutex_unlock(&channel->lock);
            ...
        }

    The caller is expected to hold the lock.  The cocci
    lock_state instrumentation models lock state as ghost; with
    an empty-ghost-bootstrap the contract fires on the unlock
    site by default.  This is a caller-precondition shape that
    the per-function harness cannot validate.
    """
    # Find unlock calls and their argument.
    unlock_re = re.compile(
        r"\b(?:mutex_unlock|spin_unlock(?:_irqrestore|_bh|_irq)?|"
        r"read_unlock(?:_irqrestore|_bh|_irq)?|"
        r"write_unlock(?:_irqrestore|_bh|_irq)?|"
        r"raw_spin_unlock(?:_irqrestore|_bh|_irq)?)\s*\(\s*"
        r"&?([A-Za-z_][\w.\->]*)")
    lock_re = re.compile(
        r"\b(?:mutex_lock(?:_interruptible|_killable|_nested)?|"
        r"spin_lock(?:_irqsave|_bh|_irq)?|"
        r"read_lock(?:_irqsave|_bh|_irq)?|"
        r"write_lock(?:_irqsave|_bh|_irq)?|"
        r"raw_spin_lock(?:_irqsave|_bh|_irq)?)\s*\(\s*"
        r"&?([A-Za-z_][\w.\->]*)")
    unlocks = unlock_re.findall(body)
    if not unlocks:
        return None
    locks = set(lock_re.findall(body))
    # Note: arguments may be normalised differently (foo->bar
    # vs &foo->bar), so compare by the trailing identifier.
    def tail(s: str) -> str:
        for sep in ("->", ".", "&"):
            if sep in s:
                s = s.rsplit(sep, 1)[-1]
        return s
    locked_tails = {tail(s) for s in locks}
    for unlock_arg in unlocks:
        if tail(unlock_arg) not in locked_tails:
            return FilterVerdict(
                "caller_holds_lock",
                f"function calls unlock on `{unlock_arg}` "
                "without a matching lock in this body — "
                "caller holds the lock (per-function harness "
                "cannot model the precondition)",
                "medium",
            )
    return None


def _detect_local_alloc_handed_to_consumer(
        body: str) -> FilterVerdict | None:
    """Match the pattern where a local variable is allocated
    and the same variable is then passed to a function call
    without being freed locally afterwards.

    Common shapes:

      msg = nlmsg_new(...);
      ...
      genlmsg_multicast(..., msg, ...);
      // no kfree(msg) — netlink consumer takes ownership

      buf = kmalloc(...);
      usb_fill_bulk_urb(urb, ..., buf, ...);
      urb->transfer_flags |= URB_FREE_BUFFER;
      // no kfree(buf) — URB owns it now

      tag = kstrdup(...);
      cache->tag = tag;        # caught by escape_via_store
      // no kfree(tag) — cache owns it now

      challenge = kmemdup(...);   # writes through out-ptr
      *out_ptr = challenge;
      // no kfree(challenge) — caller owns it now

    Heuristic: `var` is allocated via a known alloc API, then
    appears as an argument inside ANY function-call expression,
    and there is NO `kfree(var)` / `kvfree(var)` / `kfree_skb(var)`
    in the same body.

    Conservative: we only match when the alloc-API variable is
    a non-temporary local (matches the `\bvar\s*=` form rather
    than `var = (cast)alloc(...)`) and it appears at least once
    inside a function-call argument list.
    """
    # Find local-var FRESH-allocations: `var = <fresh-alloc-api>(...)`.
    # Refcount-get APIs (dget, kref_get, kobject_get, ...) are
    # excluded: a `var = dget(x); dget_parent(var); dput(var)`
    # pattern is balanced refcount, NOT ownership transfer to
    # dget_parent.  Firing alloc_handed_to_consumer on those
    # would suppress real refcount-balance bug detections (e.g.
    # CVE-2025-21654 in ovl_connect_layer).
    fresh_alloc_re = re.compile(
        r"(\b(?:[A-Za-z_][A-Za-z_0-9]*)\b)\s*=\s*"
        r"(?:" + FRESH_ALLOC_APIS + r")\s*\(",
    )
    alloc_vars = set()
    for m in fresh_alloc_re.finditer(body):
        alloc_vars.add(m.group(1))
    for var in alloc_vars:
        esc = re.escape(var)
        # Count uses of `var` as an argument inside a non-free
        # function call.  We deliberately do NOT short-circuit
        # on the presence of `kfree(var)` etc. — error-path
        # frees often coexist with a success-path consumer
        # call; the cocci leak detector still fires falsely
        # because it sees the success path as un-freed.
        free_re = re.compile(
            r"\b(?:" + PUT_APIS + r"|nlmsg_free|"
            r"sk_free|free_irq|usb_free_urb|usb_kill_urb|"
            r"consume_skb|kfree_skb_partial)\s*\(\s*"
            + esc + r"\b")
        # Iterate every function-call expression that mentions `var`.
        seen_consumer = False
        call_re = re.compile(
            r"\b([A-Za-z_]\w*)\s*\(([^()]*)\)")
        for m in call_re.finditer(body):
            args = m.group(2)
            # Quick reject if var not in this call.
            if not re.search(r"\b" + esc + r"\b", args):
                continue
            # Skip if the entire call is a free of var.
            full = m.group(0)
            if free_re.match(full):
                continue
            seen_consumer = True
            break
        if seen_consumer:
            return FilterVerdict(
                "alloc_handed_to_consumer",
                f"local `{var}` is allocated and passed to "
                "another function as an argument; the callee "
                "takes ownership (per-function leak check "
                "fires falsely on the success path)",
                "medium",
            )
    return None


def _detect_alloc_into_param_field(body: str
                                   ) -> FilterVerdict | None:
    """Match patterns where an allocation result is written
    directly into a caller-owned location (no local variable
    intermediary).  Three sub-shapes:

    1. `<param>-><field> = alloc(...)` — store into struct
       field of a parameter.  E.g. `dev->absinfo = kcalloc(...)`
       in input_alloc_absinfo.
    2. `*<param> = alloc(...)` — store through a single-level
       out-pointer.  E.g. constructor-style helpers that
       return an object via an OUT pointer.
    3. `*<param><field> = alloc(...)` and `**<param> = alloc(...)`
       — pointer-to-pointer out parameter.  E.g.
       `*challenge = kmemdup(...)` in auth_parse.

    All three transfer ownership to the caller-owned location;
    the per-function leak check fires falsely because it sees
    no kfree of the alloc result locally.
    """
    pats = (
        # 1. <param>-><field> = alloc(...)
        re.compile(
            r"\b[A-Za-z_]\w*\s*->\s*[A-Za-z_]\w*\s*=\s*"
            r"(?:" + ALLOC_APIS + r")\s*\("),
        # 2. *<param> = alloc(...)  (single-level out-pointer)
        re.compile(
            r"(?:^|[^\w])\*\s*[A-Za-z_]\w*\s*=\s*"
            r"(?:" + ALLOC_APIS + r")\s*\("),
        # 3. **<param> = alloc(...)  (double-pointer out param)
        re.compile(
            r"(?:^|[^\w])\*\*\s*[A-Za-z_]\w*\s*=\s*"
            r"(?:" + ALLOC_APIS + r")\s*\("),
    )
    for pat in pats:
        if pat.search(body):
            return FilterVerdict(
                "escape_via_store",
                "allocation result is written directly into "
                "a caller-owned location (parameter field or "
                "out-pointer) — ownership transfers to the "
                "caller (per-function leak check fires falsely)",
                "high",
            )
    return None


def _detect_constructor_with_out_pointer(
        fn_name: str, body: str | None) -> FilterVerdict | None:
    """Return a verdict if `fn_name` is itself a constructor-
    style function (returns an allocated object via an
    out-pointer parameter or directly).

    Common kernel idiom:
      int snd_midi_event_new(int bufsize, struct snd_midi_event **rdev) {
          *rdev = kzalloc(...);
          (*rdev)->buf = kmalloc(...);
          return 0;
      }

    Sub-allocations stored through the out-pointer are
    transferred to the caller; the catalog's per-function
    leak check naturally flags them as outstanding even
    though ownership has transferred.

    Detection: function name ends in `_new`, `_alloc`,
    `_create`, or `_init` AND body contains an assignment
    of the form `*<param> = X;` where X is an alloc-API
    result OR a tracked variable.

    The name-only check is used when body is unavailable.
    """
    name_match = (fn_name.endswith("_new")
                  or fn_name.endswith("_create")
                  or fn_name.endswith("_alloc")
                  or fn_name.endswith("_init"))
    if not name_match:
        return None
    if body is None:
        # Name match is a strong-but-not-definitive hint;
        # only flag as ownership-handler if the body is
        # unavailable.  Tag confidence accordingly.
        return FilterVerdict(
            "ownership_handler",
            f"function name '{fn_name}' suggests a constructor "
            "(transfers allocated object to caller); "
            "leak preconditions are caller-ensured "
            "(out of harness scope)",
            "medium",
        )
    # Body present: look for `*<word> = ...` patterns.
    if re.search(
            r"\*\s*\w+\s*=\s*"
            r"(?:k[zv]?(?:alloc|malloc|calloc)|"
            r"alloc_skb|kmem_cache_(?:alloc|zalloc))",
            body):
        return FilterVerdict(
            "ownership_handler",
            f"function `{fn_name}` is a constructor: "
            "allocates and stores the result in an "
            "out-pointer parameter, transferring "
            "ownership to the caller",
            "high",
        )
    # The earlier 'medium-confidence' two-step form
    # (`X = alloc(); ... *out = X;`) was removed because the
    # `\*\s*\w+\s*=\s*\w+` pattern matched declarations like
    # `struct foo *mapping = expr;` that are NOT ownership
    # transfers.  When two real candidates moved between
    # buckets in n=200 v9 (CVE-2024-35829 lima_heap_alloc was
    # fp-filtered as a false-positive of this rule), the
    # medium-confidence detector was retired.  We accept some
    # missed constructor-FPs in exchange for not mis-filtering
    # real bugs.
    return None


def _detect_ownership_handler(fn_name: str) -> FilterVerdict | None:
    """Return a verdict if `fn_name` is itself an ownership-
    handler whose contract preconditions are caller-ensured
    rather than function-internal."""
    for p in _OWNERSHIP_FN_PREFIXES:
        if fn_name.startswith(p):
            return FilterVerdict(
                "ownership_handler",
                f"function name '{fn_name}' indicates an "
                "ownership-handling operation; preconditions "
                "are caller-ensured (out of harness scope)",
                "high",
            )
    for s in _OWNERSHIP_FN_SUFFIXES:
        if fn_name.endswith(s):
            return FilterVerdict(
                "ownership_handler",
                f"function name '{fn_name}' indicates an "
                "ownership-handling operation; preconditions "
                "are caller-ensured (out of harness scope)",
                "high",
            )
    # Infix matches: `<prefix>_free_<rest>`, `<prefix>_release_<rest>`,
    # `<prefix>_destroy_<rest>`, etc.  Captures kernel naming
    # styles like 'qlcnic_82xx_free_mac_list' that the
    # prefix/suffix rules miss.
    for tok in ("_free_", "_release_", "_destroy_",
                "_cleanup_", "_clean_", "_remove_", "_del_",
                "_delete_", "_disconnect_", "_unregister_",
                "_unbind_", "_deinit_", "_uninit_", "_fini_"):
        if tok in fn_name:
            return FilterVerdict(
                "ownership_handler",
                f"function name '{fn_name}' contains "
                f"'{tok.strip('_')}' indicating cleanup; "
                "preconditions are caller-ensured",
                "medium",
            )
    return None


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------

def classify(kernel_file: str | Path, fn_name: str,
             var_hint: str | None = None,
             module: str | None = None) -> FilterVerdict:
    """Classify a `failed` verdict on (kernel_file, fn_name).

    If `var_hint` is None we enumerate plausible candidate
    variables (allocs, locals, parameters) and run both
    detectors on each.  The first match wins.

    `module` is the bug-class module being checked (e.g.
    'resource_leak_on_error_path', 'use_after_free_generic',
    'lock_state').  When supplied, detectors that only
    explain FPs for specific modules are skipped for other
    modules.  This prevents over-suppression: e.g. an
    integer-overflow bug in a function that allocates and
    transfers a buffer should NOT be filtered as 'leak FP',
    because the bug is the integer overflow, not the leak.

    Without `module`, all detectors run (legacy behaviour).
    """
    # Module-to-shape applicability map.  Only suppress the
    # listed shapes for the listed modules.  Shapes not in
    # the map are universal (e.g. ownership_handler is a
    # naming-convention signal that works across all modules).
    LEAK_MODULES = {
        "resource_leak_on_error_path",
        "skb_lifetime", "fput_lifetime",
        "cred_lifetime", "refcount_lifetime",
        "kobject_lifetime", "device_lifetime",
        "of_node_lifetime", "inode_lifetime",
        "dentry_lifetime", "sock_lifetime",
        "module_lifetime", "kref_lifetime",
        "pipe_buffer", "aead",
    }
    LEAK_SHAPES = {
        "escape_via_store",
        "alloc_handed_to_consumer",
        "put_only_on_error",
        # Note: ownership_handler is universal — it's a name
        # pattern that signals caller-precondition for any
        # bug-class.  Not module-restricted.
    }
    LOCK_SHAPES = {"caller_holds_lock"}
    LOCK_MODULES = {"lock_state", "rcu_read"}
    NETLINK_SHAPES = {"netlink_caller_validated"}
    NETLINK_MODULES = {"netlink_attr_validation"}

    def shape_applies(shape: str) -> bool:
        """Return True if the FP shape applies to this module
        check.  When module is None, all shapes apply."""
        if module is None:
            return True
        if shape in LEAK_SHAPES:
            return module in LEAK_MODULES
        if shape in LOCK_SHAPES:
            return module in LOCK_MODULES
        if shape in NETLINK_SHAPES:
            return module in NETLINK_MODULES
        return True

    def gate(v: FilterVerdict | None) -> FilterVerdict | None:
        """Return the verdict only if its shape applies to
        the current module."""
        if v is None or v.shape is None:
            return v
        return v if shape_applies(v.shape) else None

    kp = Path(kernel_file)
    if not kp.exists():
        return FilterVerdict(None, "source not found", "high")
    try:
        source = kp.read_text(errors="replace")
    except OSError:
        return FilterVerdict(None, f"unreadable: {kp}", "high")
    body = _function_body(source, fn_name)
    if body is None:
        # Try name-based detectors when source-extraction
        # fails — both constructor and ownership-handler
        # patterns are detectable from the name alone.
        v_constructor = gate(
            _detect_constructor_with_out_pointer(fn_name, None))
        if v_constructor is not None:
            return v_constructor
        v_name = gate(_detect_ownership_handler(fn_name))
        if v_name is not None:
            return v_name
        return FilterVerdict(None, f"function `{fn_name}` not "
                                   "found", "high")

    # Constructor / ownership-handler name-and-body checks
    # run before per-variable body detectors.
    v_constructor = gate(_detect_constructor_with_out_pointer(
        fn_name, body))
    if v_constructor is not None:
        return v_constructor
    v_name = gate(_detect_ownership_handler(fn_name))
    if v_name is not None:
        return v_name
    # Body-only check: alloc result stored directly into
    # a parameter struct field (no local intermediate var).
    v_field = gate(_detect_alloc_into_param_field(body))
    if v_field is not None:
        return v_field
    # Body+signature check: any pointer-typed parameter that's
    # passed to another function and never derefed here.
    pn = _function_param_names_pointer_typed(source, fn_name)
    v_consumed = gate(_detect_param_consumed_by_callee(body, pn))
    if v_consumed is not None:
        return v_consumed
    # Body-only check: a local alloc'd var is passed to a
    # consumer-API and never freed locally (msg → genlmsg_multicast,
    # buf → usb_fill_bulk_urb, tag → cache->tag, etc.)
    v_handed = gate(_detect_local_alloc_handed_to_consumer(body))
    if v_handed is not None:
        return v_handed
    # Body-only check: function unlocks a lock without a
    # matching lock in the same body — caller-holds-lock
    # precondition shape.
    v_unlock = gate(_detect_unmatched_unlock(body))
    if v_unlock is not None:
        return v_unlock
    # Body+signature check: function reads netlink attrs but
    # doesn't allocate/free; caller validated via
    # nla_parse_nested.
    v_nla = gate(_detect_netlink_caller_validated(
        source, fn_name, body))
    if v_nla is not None:
        return v_nla

    # Build a list of candidate variables to test.
    candidates: list[str] = []
    if var_hint:
        candidates.append(var_hint)
    # 1. Allocations: var = alloc_api(...)
    for m in ALLOC_RE.finditer(body):
        if m.group(1) not in candidates:
            candidates.append(m.group(1))
    # 2. get_X(var) calls — first arg of any get-style API.
    for m in re.finditer(
        r"\b(?:" + ALLOC_APIS + r")\s*\(\s*"
        r"([A-Za-z_][A-Za-z_0-9]*)\b",
        body,
    ):
        if m.group(1) not in candidates:
            candidates.append(m.group(1))
    # 3. put_X(var) calls — surface the variable being put.
    for m in PUT_RE.finditer(body):
        if m.group(1) not in candidates:
            candidates.append(m.group(1))
    # 4. Local pointer declarations: `T *var;` or `T *var = ...;`
    for m in re.finditer(
        r"\b(?:struct\s+\w+|const\s+\w+|\w+_t)\s+\*\s*"
        r"([A-Za-z_][A-Za-z_0-9]*)\s*[=;,)]",
        body,
    ):
        if m.group(1) not in candidates:
            candidates.append(m.group(1))

    # Deduplicate and limit to a reasonable count.
    candidates = candidates[:20]

    if not candidates:
        return FilterVerdict(None, "no candidate variables "
                                   "found", "low")

    # Run detectors against each candidate.  The first
    # high-confidence match wins.  Module-gate the verdicts
    # so we don't suppress real candidates in modules where
    # leak-shaped FPs aren't applicable (e.g. integer-overflow
    # bug in an alloc-and-return function).
    best: FilterVerdict | None = None
    for var in candidates:
        v = gate(_detect_escape_via_store(body, var))
        if v is not None:
            if v.confidence == "high":
                return v
            if best is None or best.confidence == "low":
                best = v
        v = gate(_detect_put_only_on_error(body, var))
        if v is not None:
            if best is None or best.confidence == "low":
                best = v

    if best is not None:
        return best
    return FilterVerdict(None, f"no FP signature on "
                               f"{len(candidates)} candidate(s)",
                         "low")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(
        description="Classify a per-file failed verdict as one of "
                    "the known false-positive shapes.")
    ap.add_argument("kernel_file")
    ap.add_argument("fn_name")
    ap.add_argument("--var", help="Variable name hint")
    args = ap.parse_args()
    v = classify(args.kernel_file, args.fn_name, args.var)
    print(f"shape:      {v.shape!r}")
    print(f"confidence: {v.confidence}")
    print(f"reason:     {v.reason}")
