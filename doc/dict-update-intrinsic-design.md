# Design Note: `cprover_dict_update_func` Solver Intrinsic

## Motivation

The Python frontend currently implements `dict.update(other)`
for literal `other` by emitting a scan-and-replace loop per
entry. For a 5-entry source dict and 16-slot bounded target,
that's 5 × 16 = 80 guarded GOTO statements plus 5 append-if-
absent guards. The profiling run showed dict-update-mutation
as the slowest regression test (~170 ms vs 50 ms median).

A dedicated solver intrinsic would replace the O(entries ×
slots) unroll with a single axiom expressing "the updated
dict contains all of other's entries, plus all of d's
entries whose keys don't collide with other."

## Proposed Interface

```
int cprover_dict_update_func(
    int length_out, ptr keys_out, ptr values_out,   // result
    int length_in,  ptr keys_in,  ptr values_in,    // original d
    int length_upd, ptr keys_upd, ptr values_upd);  // other
```

Return code is the new length of d after merge. The solver
would add axioms:

1. For every i in [0, length_upd): the output contains
   (keys_upd[i], values_upd[i]) — either at an index where
   keys_in already had keys_upd[i] (replace), or at an
   appended slot (new).
2. For every i in [0, length_in): if keys_in[i] is NOT in
   keys_upd[0..length_upd], the output contains
   (keys_in[i], values_in[i]) at some index.
3. length_out == length_in + (number of upd keys not in
   keys_in[0..length_in]).

## Why Deferred

Implementation requires:

- A new IREP_ID_ONE in src/util/irep_ids.def.
- A new `add_axioms_for_dict_update` in
  src/solvers/strings/ (though "strings" is where
  refinement functions live — dict might need its own
  module).
- Axiom generation covering the three rules above.
- Registration in `make_array_pointer_association` and
  `add_axioms_for_function_application` (for the parse
  and the dispatch paths).
- Coordination with the refinement loop to handle the
  axioms' quantifiers.

Rough estimate: 300-500 lines across 3-4 files, plus solver
testing. Would speed up dict-heavy code substantially but
doesn't move the correctness needle.

## Current State

The slot-scan path emits proper mutation that tracks
literals correctly (see dict-update-mutation regression).
Performance is acceptable: 170 ms for 10 merged entries on
a 16-slot bounded dict. Linear growth in entry count,
O(entries × PYTHON_MAX_DICT_SIZE) worst case.

## Alternative: Parse-Time Literal Fusion

A narrower optimization that doesn't require solver work:
when BOTH `d` and `other` are known literals at parse time
(tracked in `dict_literals`), compute the merged dict in
C++ and re-register the result as the new `dict_literals`
entry for `d`. The mutation statements still get emitted
for soundness when non-literal reads subsequently access
`d` via a function argument, but the fast path becomes
single-lookup for literal-local code.

This is ~40 lines in python_converter.cpp and captures the
most common pattern in user code (building up a config dict
with several `.update()` calls before passing to library
code). A future commit can add this without the solver
detour.
