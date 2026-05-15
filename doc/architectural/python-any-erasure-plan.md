\file
# Plan: detecting `Any`-erasure bugs across function boundaries

This is a design note for the `bedrock_data_automation_example`
benchmark MISS, and for the broader class of bugs that flow through
function parameters annotated `Any` (or left unannotated, which we
normalise to `Any`).

## The bug pattern

```python
def create_client(region: str = 'us-east-1') -> Optional[BedrockDataAutomation]:
    client: BedrockDataAutomation = boto3.client('bedrock-data-automation', ...)
    return client


def process_document(client: Any, content: str, doc_type: str = 'text') -> Any:
    response = client.invoke_data_automation_async(...)
    return response


main():
    client = create_client()
    if client:
        result = process_document(client, sample_text, 'text')
        ...
```

`invoke_data_automation_async` exists on
`BedrockDataAutomationRuntime`, **not** on `BedrockDataAutomation`.
The user picked the wrong client. With our existing
attribute-error detection we'd catch the typo at the call site if
the receiver had a known concrete type — the missing-method check
fires when `class_types[receiver_type]` exists and the method is
absent. But the receiver here is `client: Any` inside
`process_document`, so the dispatch falls through to the
generic Any-typed receiver path and the check is silent.

## What we have today

* `class_declared_methods` collects every method (and inner class)
  declared on each class during the convert_class_def pre-pass,
  including inherited methods via the MRO. The static
  attribute-error check fires when:
  1. The receiver's static type is `struct` or `struct_tag`
     resolving to a known class.
  2. The method name isn't in the receiver class's
     `class_declared_methods`.
  3. The method isn't a boto3 BaseClient inherited helper
     (`get_paginator`, `can_paginate`, …).
  4. No enclosing `try/except` catches `AttributeError`
     specifically (catch-alls don't suppress).
* This works for direct calls and for forward-references inside
  the same class.
* It does **not** handle calls where the receiver flows through a
  function parameter typed `Any`.

## Approaches considered

### A — Full call-site specialization

For each call `f(x, y, …)` where some parameter `i` of `f` is
annotated `Any` (or unannotated) and the caller's argument has a
more specific static type, generate a specialised clone of `f`:

* Symbol id: `python::f__specialized_<hash-of-param-types>`.
* Body: re-convert `f`'s AST with the parameter symbol retyped.
* Replace the call site to refer to the cloned symbol.

Pros:

* Maximally precise: any use of the parameter inside the body —
  direct method call, alias assignment, nested call with the
  parameter as an argument, return — gets the concrete type.
* Subsumes Approach C below as a special case.

Cons:

* Combinatorial blowup: a function called from N call sites with
  M distinct parameter-type vectors yields ≤ M·N specialized
  symbols. Real-world Python is often well-bounded (most stub
  classes are unique receivers), but worst-case is unbounded.
* Recursion: a specialised clone may call itself; need to detect
  recursion-on-the-same-clone and either keep the call recursive
  in the clone or fall back to the unspecialised function.
* Aliasing: if `f` is captured by reference (`g = f; g()`), every
  call site of `g` must use the right specialisation. Tracking
  this through arbitrary Python aliasing is hard.
* Cost: ~500 lines of new code plus a careful test suite, plus
  symbol-table churn during clone instantiation.

### B — Inter-procedural type propagation (IPTP)

Run a pre-pass over the call graph in topological order, computing
for each function a per-parameter "may-have-this-type" set. At
each call site within the body, narrow the receiver's type using
the computed set. Use the narrowed type for the missing-method
check.

Pros:

* No symbol-table cloning.
* Scales linearly in functions × call sites for the analysis
  itself.

Cons:

* Substantial new infrastructure: call-graph construction, fixed-
  point iteration, MRO-aware union of types, polymorphic dispatch
  modelling.
* Hard to reuse: most of CBMC's analyses are over goto programs,
  not Python ASTs. A new analysis at the AST level would be
  parallel infrastructure.
* Cost: ~1200 lines plus integration; high blast radius.

### C — Caller-side body sniff

At each call site `f(args)`:

1. Parse `f`'s AST body looking for `Attribute(Name(param_i), X)`
   chains where `param_i` is a parameter of `f` and `X` is a
   method or attribute reference.
2. For each (param_i, X), if the caller's argument `i` has a known
   class, look up `X` as a method on that class:
   * If absent and not a known boto3-base method, emit an
     `attribute-error` property tagged with `f`'s line for `X`,
     anchored at the caller's source location.
3. The function `f`'s body is still converted normally (with the
   `Any`-typed parameter); we add the property as a side-channel.

Pros:

* Targeted: handles the exact `bedrock_data_automation_example`
  pattern.
* Cheap: ~150 lines, contained to one helper.
* No symbol-table cloning, no recursion concern, no fixed-point.
* Easy to extend incrementally to more attribute kinds.

Cons:

* Only catches `param.X(…)` directly. Misses:
  * `tmp = param; tmp.X(…)` (alias).
  * `g(param)` flowing further (one-hop inter-procedural).
  * `return param` with downstream usage.
* False negatives in real but uncommon dataflow patterns.

### D — Hybrid: C now, A as the future strict mode

Land Approach C as the immediate fix (covers the benchmark). Keep
Approach A in reserve as `--python-specialise-any-params` for
strict mode, similarly to how `--python-required-kwarg-checks`
gates a stricter body of detection.

## Recommendation

**Land Approach C first as the new opt-in flag
`--python-check-any-arg-attrs`** (off by default). When enabled,
emit attribute-error properties at call sites where the caller's
argument has a known concrete type and the callee's parameter
annotation is `Any`.

Defer Approach A to a follow-up plan if benchmark data shows it
would catch additional real bugs.

## Approach C — detailed implementation

### Data structures

Add to `python_convertert`:

```cpp
/// Map from python::<func>::<param_name> → set of attribute names
/// referenced via 'param.<name>' in the function body. Populated
/// during convert_function_def's body walk, before any per-call
/// processing. Used by --python-check-any-arg-attrs at each call
/// site to verify the caller's arg type provides those
/// attributes.
std::map<irep_idt, std::set<std::string>> function_param_attr_uses;
```

### Pre-pass: collect attribute uses per parameter

Inside `convert_function_def`, before converting the body for
real, run a recursive AST walker that visits every statement and
expression looking for nodes of shape:

```
Attribute(value=Name(id=p), attr=X)
```

where `p` is one of the function's declared parameter names. For
each such match, record `(python::<func>::<p>, X)` in
`function_param_attr_uses`.

The walker must descend into:

* All compound statements (If, For, While, Try, With, FunctionDef
  is excluded — nested defs have their own params).
* Expression-bearing statements (Expr, Return, Raise, Assert,
  AugAssign, AnnAssign).
* Sub-expressions (Call args/kwargs, BinOp/UnaryOp/BoolOp/Compare
  operands, Subscript value+slice, IfExp test/body/orelse, etc.).

Lambdas and nested function defs are skipped — they bind the
parameter name fresh.

This walker also records assignments `tmp = param` as a
"`tmp`-aliases-`param`" entry in a side map, so a follow-up walk
can collect attributes used through `tmp`. For Approach C's first
landing we ignore aliases (FN); aliasing follow-up is a small
extension of the same walker.

### Per-call-site check

At each call site `convert_call(f, [a0, a1, …])`:

1. Look up `f`'s symbol; obtain its declared parameter list.
2. For each parameter index `i`:
   a. If `f`'s parameter `i` is **not** annotated `Any`/unannotated,
      skip (we handle that case via the existing convert_call
      typecheck).
   b. If the caller's argument `i` has a static type that doesn't
      resolve to a class in `class_types`, skip (we don't have a
      schema to check against).
   c. Look up `function_param_attr_uses[python::<f>::<param_name>]`.
      For each attribute name `X` in the set:
      * If `X` is in `class_declared_methods[caller_type]` (own
        or inherited via MRO), no problem.
      * If `X` is in the boto3-base-methods allowlist, no problem.
      * Otherwise, emit an attribute-error property at the **call
        site** with comment:
        `"caller passes <CallerT>, but <f> uses param.<X>;
         <CallerT> has no attribute '<X>'"`.
3. The attribute-error property is gated by
   `python_check_any_arg_attrs`, off by default.

### Behaviour on the bedrock benchmark

* `process_document(client: Any, …)` body uses
  `client.invoke_data_automation_async(…)`. The walker records:
  `function_param_attr_uses[python::process_document::client] =
   {"invoke_data_automation_async"}`.
* At `process_document(client, sample_text, 'text')` in main,
  the caller's `client` has static type `BedrockDataAutomation`.
  `class_declared_methods["BedrockDataAutomation"]` does not
  contain `invoke_data_automation_async` (that lives on the
  Runtime client).
* We emit `attribute-error: caller passes BedrockDataAutomation,
   but process_document uses param.invoke_data_automation_async;
   BedrockDataAutomation has no attribute
   'invoke_data_automation_async'`.

Verification fails → TP.

### False-positive risk

* When the user's own code defines a class with a missing method
  but the caller relies on the method via the parameter, we'd
  fire. That IS a bug — keep the property.
* When the caller's argument is conservatively typed (e.g. `Any`),
  we skip. Good.
* When the caller's argument resolves to a class for which our
  `class_declared_methods` is incomplete (e.g. boto3 stub class
  inherits from BaseClient implicitly and the inherited method
  isn't in our allowlist), we'd fire spuriously. Mitigation:
  the existing boto3-base allowlist; extend with method names
  observed in widely-used stubs (`get_paginator`, `meta`,
  `exceptions`, `close`, `generate_presigned_url`,
  `generate_presigned_post`, `can_paginate`, `get_waiter`).
  Already present.
* When the parameter's declared type is `Optional[T]` (i.e. T or
  None), the caller may legitimately pass None and the attribute
  access would AttributeError at runtime. Conservative: only
  emit when caller's arg is non-None (concrete struct, not
  null pointer). Rule out the None case by skipping when the
  arg is a constant `None` literal or has Optional-wrapped type.

### Milestones

1. Add `function_param_attr_uses` and the AST pre-pass walker.
   Verify against a regression test that records expected attr
   sets.
2. Add the `--python-check-any-arg-attrs` flag.
3. Wire the per-call-site check.
4. Regression tests:
   * `attribute-error-any-erasure-pos`: bug-bearing case (the
     bedrock_data_automation_example pattern, simplified).
   * `attribute-error-any-erasure-neg`: caller passes correct
     class, no false positive.
   * `attribute-error-any-erasure-base`: caller passes
     `Any` itself; check skipped (no FP).
   * `attribute-error-any-erasure-optional`: caller's arg is
     Optional/None-able; check skipped.
5. Run the full benchmark suite under both default and the new
   flag. Confirm bedrock_data_automation_example moves
   MISS → TP and no other benchmark regresses.

### Estimated cost

* Walker + data structures: ~80 lines.
* Flag wiring: ~15 lines.
* Per-call check: ~70 lines.
* Regression tests: 4 small files.
* Total: ~165 lines + tests.

## Future work — Approach A

When and only when benchmarks demonstrate Approach C's
limitations matter (alias chains, multi-hop flows), revisit
Approach A. Recommended sequencing:

1. Implement function cloning for Any-typed parameters at single
   call sites with no aliasing.
2. Add a memoization cache on (function_id, param_type_tuple).
3. Handle single-level aliasing via Python def-use tracking.
4. Detect mutual recursion and fall back gracefully to
   unspecialised + Approach C.

Approach A's cost is ~500 lines plus a substantial test matrix.
Defer until justified.
