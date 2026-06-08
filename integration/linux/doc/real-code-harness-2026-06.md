# Real-code / interprocedural harnessing via goto-cc (prototype)

**Date:** 2026-06-08
**Files:** `tlv_parser.c`, `anybuss_chain.c`
**Goal:** address the root cause of the staging false positives —
the scaled, intraprocedural, single-`memcpy` harness can't see
(a) guards in *callers* and (b) bugs that live in *parse loops*.
This prototype shows the `goto-cc → goto-binary → cbmc` workflow
making a `FAILED` verdict *mean* a reachable OOB.

## Workflow

```
goto-cc -o x.gb file.c            # compile to goto-binary (scales to real .o)
cbmc x.gb --function H --bounds-check [--unwind N]
```

## Part A — parse-loop OOB (loop unwinding finds it)

`tlv_parser.c` models the 802.11/TLV pattern: walk `{type,len,value}`
elements advancing by the attacker-supplied `len`.  The single-copy
model cannot express this; CBMC's loop unwinding can.

| harness | verdict | property |
|---------|---------|----------|
| `harness_parse_buggy` | **FAILED** | `array 'body' upper bound in body[pos+2+i]` |
| `harness_parse_fixed` (bound `pos+2+len > total → break`) | **SUCCESSFUL** | — |

Concrete counterexample (witness) for the buggy loop:

```
total=8   element@pos0 len=2  -> pos=4
          element@pos4 len=133 -> reads body[6..138]  // OOB past body[8]
```

i.e. an element near the end declaring a large `len` walks the read
off the buffer — exactly the IE/TLV "length not bounded against
remaining buffer" bug.  `FAILED` here is a reachable OOB with a
reproducer, not a modelling artifact.

## Part B — interprocedural harness eliminates the caller-context FP

The sharpened query flagged `create_area_user_writer`'s
`copy_from_user(ap->buf, buf, count)` into a fixed
`buf[MAX_DATA_AREA_SZ]` (512).  Intraprocedurally that's FAILED
(`count` unbounded locally) — a false positive, because the caller
`anybuss_write_input` clamps
`len = min_t(loff_t, MAX_DATA_AREA_SZ - *offset, size)`.

`anybuss_chain.c` models the **real call chain** with the **real
constant** (512), so CBMC reasons about the caller's precondition:

| harness | precondition | verdict |
|---------|--------------|---------|
| `harness_chain_vfs` | `*offset >= 0` (vfs `rw_verify_area`) | **SUCCESSFUL** |
| `harness_chain_nopre` | none | **FAILED** |

Two payoffs:
1. **The FP is eliminated** — with the precondition vfs actually
   guarantees, the copy is proven in-bounds.  The scaled
   intraprocedural model could not do this.
2. **The exact safety precondition is pinned**: `*offset >= 0`.
   `harness_chain_nopre` FAILS, showing that if a caller ever
   reached this with a negative offset, `MAX_DATA_AREA_SZ - *offset`
   exceeds 512 and the copy overflows.  That is a precise, auditable
   obligation rather than a vague alarm.

## Why this is the path to more (and more credible) bugs

Every staging "candidate" this session was ultimately FP for a
reason *outside* the intraprocedural view (caller clamp, derived-var
guard, modulo bound).  Real-code/interprocedural harnessing:

* turns `FAILED` into "reachable OOB + trace" (Part A) — so triage
  trusts it;
* turns caller-guarded FPs into proofs **or** explicit
  preconditions (Part B) — so they stop wasting triage time;
* via `goto-cc` it scales toward verifying actual driver objects
  (the next step: compile a real `.o`, stub the kernel environment,
  drive the real fops/ioctl entry with nondet `__user` input and
  `__CPROVER_assume` on the real buffer fields).

These two `.c` files are the templates for stage-2 harness
generation against real code.
