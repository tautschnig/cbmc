\file
# CBMC core invariants exposed by `--python-check-annotations`

This document records two pre-existing CBMC core issues that the
`--python-check-annotations` flag surfaces on the AWS Python
benchmark suite. They are NOT introduced by the Python front-end
itself — both are CBMC-core bugs that the extra assertion checks
happen to expose. The flag therefore stays off-by-default until
both are addressed.

## Issue 1: `boolbv_map.cpp:68` invariant on `aws_untagged_resources_analyzer`

### Symptom

```
File: src/solvers/flattening/boolbv_map.cpp:68 function: get_literals
Reason: number of literals in the literal map shall equal the bitvector width
```

Triggered only when `--python-check-annotations` is active.

### Diagnosis

Instrumenting `boolbv_map::get_literals` to print the offending
identifier and the two conflicting widths reveals:

```
DBG boolbv_map mismatch:
  id              = python::UntaggedResourcesAnalyzer::get_s3_buckets::tags#1..values[[0]]
  stored_width    = 384   (struct_tag, == python_value_type)
  requested_width = 64    (signedbv)
```

The same SSA symbol is registered initially with the
tagged-union (`python_value_type`, 384 bits =
32+64+64+32+64+64+64) and later requested as a 64-bit
`signedbv`. Two different views of the same memory location at
two different widths — this is a known unsoundness pattern in
CBMC's bit-blasting layer when a struct-tag-typed value is
implicitly bit-aliased through a typecast or member-access path.

### Trigger context

The benchmark builds a dict via comprehension:

```python
tags: dict[Any, Any] = {tag['Key']: tag['Value']
                        for tag in tags_response.get('TagSet', [])}
```

Without `--python-check-annotations` the symbol is consistently
typed `python_value_type` everywhere. With the flag, the
annotation-mismatch check at the comprehension's surrounding
assignment introduces a check whose conversion path subsequently
references a 64-bit slice of the same SSA node.

### Why a minimal repro doesn't reproduce

A trivial `dict[Any, Any] = {item['k']: item.get('v', '') for ...}`
test case does not trigger the invariant. The actual benchmark
reaches this through a longer call-graph involving
`s3.exceptions.ClientError` handling and the for-loop body's
`untagged_buckets.append({…'ExistingTags': tags…})`, which in
turn forces a struct-flatten interaction. Reproducing in
isolation requires preserving the full set of types reachable
from the assertion's path constraint.

### Fix direction (deferred)

The fix is in CBMC's boolbv layer, not in the Python front-end.
Two viable approaches:

1. **Type-check on lookup**: in `get_literals`, if the requested
   type's width disagrees with the stored type's width, return a
   freshly-allocated literal map for the new type rather than
   firing the invariant. Trades the invariant for a soundness gap
   we'd need to compensate for elsewhere.
2. **Force consistent width at symbol creation**: ensure the
   front-end never emits a member-access or typecast on a
   struct-tag-typed SSA symbol that asks for fewer than the full
   tag's bits. This is the principled fix but requires auditing
   all the `--python-check-annotations` code paths to find the
   one introducing the 64-bit slice.

Either is a sizeable focused effort. Track as future work.

## Issue 2: solver `ERROR` per-property on `websocket_url_validator`

### Symptom

Running `--python-check-annotations` on the same benchmark yields
several `annotation-mismatch.N` properties whose status is
`ERROR` (rather than `SUCCESS` / `FAILURE`):

```
[annotation-mismatch.1] file ... line 55 ... : ERROR
[annotation-mismatch.3] file ... line 63 ... : ERROR
```

`ERROR` means CBMC's refinement loop terminated without
deciding the property within the iteration limit. The whole
verification run reports `VERIFICATION ERROR`.

### Diagnosis

The benchmark's `validate_websocket_url` function returns a
`typing.Dict[str, object]`. Each `return result` (lines 55, 59,
63, 71, 86) generates an annotation-mismatch property checking
the returned dict's element types against the declared annotation.

Lines 55 and 63 (early-return paths) ERROR while lines 59, 71,
86 SUCCEED. The difference is path-conditioned: the early
returns happen only when `result['error']` was just assigned a
string-typed value, mutating the dict's effective shape in a
way that makes the path constraint hard for the
string-refinement solver to discharge.

### Fix direction (deferred)

Solver-side issue, also CBMC core. Possible mitigations:

* Increase the refinement-loop iteration limit (currently
  defaults; may need to expose a CLI knob for Python source).
* Simplify the property: the annotation-mismatch on
  `dict[str, object]` returns is effectively always SUCCESS once
  `object` is `python_value_type`; we could short-circuit the
  check when the declared element is `python_value_type` (Any).

The latter is a Python-front-end-side mitigation and may be the
quickest route to unblocking this benchmark.

## Status

Both issues block default-on enablement of
`--python-check-annotations`. They are CBMC core bugs (boolbv
flattening / solver convergence respectively) and require focused
debugging beyond the scope of the recent precision-tuning round.
The flag remains off-by-default; users who know the trade-off
can enable it for individual files.
