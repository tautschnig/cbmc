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
ALLOC_APIS = (
    r"kmalloc|kzalloc|kcalloc|kmem_cache_alloc|alloc_skb|"
    r"kobject_create_and_add|prepare_kernel_cred|prepare_creds|"
    r"override_creds|get_cred|get_task_cred|"
    r"kobject_get|of_node_get|get_device|igrab|dget|fget|"
    r"sock_hold|skb_get|kref_get|try_module_get|"
    r"d_alloc|new_inode|get_file"
)

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
    "cleanup_", "commit_", "unlink_", "delete_", "del_",
    "abort_",
)
_OWNERSHIP_FN_SUFFIXES = (
    "_put", "_release", "_free", "_exit", "_destroy",
    "_cleanup", "_unlink", "_delete", "_del", "_abort",
    "_dec_and_test",
)


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
    return None


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------

def classify(kernel_file: str | Path, fn_name: str,
             var_hint: str | None = None) -> FilterVerdict:
    """Classify a `failed` verdict on (kernel_file, fn_name).

    If `var_hint` is None we enumerate plausible candidate
    variables (allocs, locals, parameters) and run both
    detectors on each.  The first match wins.

    Returns FilterVerdict with shape=None (no FP signature
    detected; verdict stays as a real candidate) or one of
    the known FP shapes.
    """
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
        v_constructor = _detect_constructor_with_out_pointer(
            fn_name, None)
        if v_constructor is not None:
            return v_constructor
        v_name = _detect_ownership_handler(fn_name)
        if v_name is not None:
            return v_name
        return FilterVerdict(None, f"function `{fn_name}` not "
                                   "found", "high")

    # Constructor / ownership-handler name-and-body checks
    # run before per-variable body detectors.
    v_constructor = _detect_constructor_with_out_pointer(
        fn_name, body)
    if v_constructor is not None:
        return v_constructor
    v_name = _detect_ownership_handler(fn_name)
    if v_name is not None:
        return v_name

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
    # high-confidence match wins.
    best: FilterVerdict | None = None
    for var in candidates:
        v = _detect_escape_via_store(body, var)
        if v is not None:
            if v.confidence == "high":
                return v
            if best is None or best.confidence == "low":
                best = v
        v = _detect_put_only_on_error(body, var)
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
