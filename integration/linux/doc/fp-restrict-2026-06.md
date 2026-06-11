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
