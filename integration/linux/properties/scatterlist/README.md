# `scatterlist` property module

Models the subset of the Linux kernel's `struct scatterlist` API that
the AF_ALG / AEAD data path uses, and exports `sgl_all_user_writable`
— the predicate that turns `page_provenance` ghost state into a
usable contract precondition for any kernel API that writes through
a destination scatterlist.

Depends on the `page_provenance` module.  Used by the `aead` module
(milestone M3) and by the `cve-2026-31431` regression (milestone M0,
kept as an abstract model; a property-module-based regression is
planned as a second, parallel harness).

See the parent [`README.md`](../README.md) for module conventions.

## API

```c
void sg_init_table(struct scatterlist *sgl, unsigned int n);
void sg_set_page  (struct scatterlist *sg, struct page *p,
                   unsigned int length, unsigned int offset);
void sg_chain     (struct scatterlist *prev, unsigned int nents,
                   struct scatterlist *next);
void sg_unmark_end(struct scatterlist *sg);
struct scatterlist *sg_next(struct scatterlist *sg);
struct page        *sg_page(struct scatterlist *sg);

/* predicate callable from __CPROVER_requires / __CPROVER_assert */
_Bool sgl_all_user_writable(struct scatterlist *sgl);
```

All functions carry CBMC contracts in `scatterlist.h`; see that
header for the full signatures.  `sg_next`, `sg_page`, and
`sgl_all_user_writable` are pure.  `sg_init_table`, `sg_set_page`,
`sg_chain`, and `sg_unmark_end` have non-empty `__CPROVER_assigns`
clauses so callers' own assigns tracking can account for them.

## Representation

The module's `struct scatterlist` uses explicit `chain` and `end`
fields rather than the kernel's bit-packed `page_link` low bits.
This keeps contract predicates free of bit-twiddling.

When the module is linked against real kernel source, one of two
adapters will be applied (to be decided in milestone M3 against real
`crypto/algif_aead.c`):

- **Substitution.**  The harness `#include`s our `scatterlist.h`
  *before* any kernel header, so our `struct scatterlist` wins and the
  kernel's definition is ignored.  Requires that kernel callers use
  only the API functions and never poke the fields directly.

- **Translation.**  The harness keeps the kernel's `struct
  scatterlist` and provides thin `sg_chain` / `sg_next` /
  `sg_unmark_end` wrappers that map between the two encodings.
  Required if any kernel code inspects the bits directly.

The kernel's `crypto/algif_aead.c` uses only the API functions, so
substitution is expected to suffice; translation is kept as a
fallback.

## Bounded-chain traversal

`sgl_all_user_writable` folds over the chain with a
`for`-bounded loop of at most `SG_CHAIN_MAX_STEPS` iterations
(default 16).  If a harness builds deeper chains than that, it
raises the bound with `-DSG_CHAIN_MAX_STEPS=<n>`; otherwise the
predicate conservatively returns false on overflow so soundness is
preserved.

## Tests

- `test_unit.c` — three cases exercising
  `sgl_all_user_writable`: a lone user-writable page (expected
  true), an in-place page-cache entry (expected false), and the
  CVE-2026-31431 chain shape (expected false).  All three assertions
  must pass under plain cbmc.
- `test_replace.c` — declares a hypothetical kernel API
  `write_sgl(dst)` with
  `__CPROVER_requires(sgl_all_user_writable(dst) == 1)`, plus a
  good and a bad caller.  Run via `goto-instrument
  --replace-call-with-contract write_sgl`; the bad caller's
  precondition must fail.

Both tests are exercised by `run.sh`.

## Known gaps

- The contract on `sg_init_table` is not enforceable via
  `goto-instrument --enforce-contract` because its body contains a
  loop (see [../../CBMC_LIMITATIONS.md](../../CBMC_LIMITATIONS.md)
  entry LIM-002).  The other mutators (`sg_set_page`,
  `sg_chain`, `sg_unmark_end`) are loop-free and their contracts
  could be enforced as a follow-up.
- `sgl_all_user_writable` is also loop-bodied; the same limitation
  applies to enforcing its (trivial) contract.  The predicate remains
  useful inside requires clauses where CBMC evaluates it symbolically.

## Versions

Validated against CBMC 6.9.0 (`build/bin/cbmc`).  Does not require
kernel headers.
