# Automated function-pointer restriction (2026-06)

**Date:** 2026-06-11.  Addresses the CBMC discharge blocker found on the
net/sched candidates: whole-function CBMC inlines SPURIOUS function-pointer
targets.  CBMC resolves an indirect call to every type-compatible
address-taken function; in the kernel that set includes unrelated functions
that merely match the signature (e.g. `dev->netdev_ops->ndo_setup_tc(...)`
in sch_mqprio.c resolves to `mqprio_init` / `mqprio_dump_class_stats`), and
inlining those large functions blows up symex.

## `fp_restrict.py` -- automated candidate identification + restriction

Per indirect call site it computes, automatically:

* **type-compatible candidates** -- from `goto-instrument
  --remove-function-pointers` verbosity (what CBMC would use);
* the pointer's **field name** -- from `--show-goto-functions`;
* **assignment-based (points-to) candidates** -- the functions whose address
  is actually stored in a field of that name, parsed from
  `--show-symbol-table` (e.g. `init=mqprio_init`,
  `dump_stats=mqprio_dump_class_stats`).

The assignment-based set is a TIGHTER, sound over-approximation than the
type-based set.  Each call site is classified:

* **NARROW** -- assignment-based set is a non-empty strict subset; the tool
  applies `goto-instrument --restrict-function-pointer
  <func>.function_pointer_call.<N>/<targets>` (the labelled call-site form --
  the by-name form does not match member-access pointers), dropping the
  spurious matches.
* **EXTERNAL** -- no in-TU function is assigned to the field, so the real
  target lives in another TU (e.g. a driver's `netdev_ops`).  Reported; the
  principled treatment is to model the call as an opaque havoc stub (the
  maximal sound over-approximation of an unknown callee), a documented
  follow-up since `--restrict-function-pointer` needs >=1 in-binary target.
* **unchanged** -- assignment-based == type-based.

```
fp_restrict.py <gb> --report
fp_restrict.py <gb> --apply <out.gb>
```

## Validation

* synthetic (`call_foo` through `ops.foo`, with `spurious` bound to
  `ops.bar`): `--report` -> NARROW 3->2; `--apply` -> the restricted binary
  dispatches `call_foo` to `{impl_a, impl_b}` only (`spurious` excluded),
  confirmed via `--show-goto-functions`.
* `sch_mqprio.gb`: all 4 indirect calls classified **EXTERNAL** (type
  candidates `mqprio_init`/`mqprio_dump_class_stats` are NOT bound to
  `ndo_setup_tc`/`fn` -> real targets out-of-TU).  This is the correct
  automated finding: the spurious type-matches are not real targets.

## Honest status

The tool delivers the automated candidate identification and the NARROW
restriction application.  For the net/sched candidates specifically, all
indirect calls are EXTERNAL, so narrowing does not apply -- and separately,
their whole-function intractability is dominated by nondet-pointer reasoning
over the full call graph (it does not complete even at unwind 2), so the
verbatim-slice discharge (cbmc-discharge-netsched-2026-06.md) remains the
right tool for those properties.  The EXTERNAL -> havoc-stub path (which
WOULD make such whole-function discharges tractable) is the next increment.

## Update: EXTERNAL -> havoc-stub path (`--stub-external`)

For EXTERNAL call sites (real target out-of-TU), the tool now routes the call
to a nondet/havoc STUB instead of leaving CBMC to inline the spurious
type-matched in-TU function.  Mechanism (reusing an existing type-compatible
candidate as the stub symbol, since `--restrict-function-pointer` needs an
in-binary target):

1. `goto-instrument --remove-function-body <cand>` -- drop the spurious
   candidate's real body;
2. `--generate-function-body '^(<cands>)$' --generate-function-body-options
   nondet-return` -- regenerate it as a nondet stub (use `havoc` for the
   sound-everywhere model that also havocs pointee args);
3. `--restrict-function-pointer <func>.function_pointer_call.<N>/<cand>` --
   dispatch the EXTERNAL call to the (now stub) symbol.

```
fp_restrict.py <gb> --stub-external <out.gb> [--stub-option nondet-return|havoc]
```

Sound for a `--function <target>` discharge: the reused stub symbol is reached
only via the restricted dispatch.

**Validation** (`extstub.c`): `target()` does `a[4]` writes then calls
`o->cb(n)`, where `.cb` is unbound in-TU and the only type-compatible
candidate `big` (a 64x64-loop function bound to `.other`) is spurious.
`--report` -> EXTERNAL; `--stub-external` -> `big` becomes a nondet-return
stub (confirmed: body is `return_value := nondet`, no loops) and `target`'s
`o->cb` dispatches to it.  CBMC then finds the `a[4]` OOB
(`array 'a' upper bound: FAILURE`) in **0.02s** -- the spurious `big` inline
that would otherwise be unwound is gone.

**mqprio (honest):** `--stub-external` applies cleanly, but
mqprio_enable_offload still does not complete -- its blowup is dominated by
the DIRECT call `mqprio_fp_to_offload` (loops over fp[16]) and nondet-pointer
reasoning over `qopt`/`priv`/`dev`, NOT the fn-ptr.  So fn-ptr stubbing is
necessary-not-sufficient there; the verbatim slice remains the right tool for
that property.  The stub path pays off where a spurious fn-ptr inline is the
dominant cost (the synthetic case, and large dispatch-heavy callees).
