# Property modules

A _property module_ packages the correct-use protocol of a kernel
primitive as CBMC function contracts, a reference implementation that
the contracts are proved consistent with, and (later) a Coccinelle
prefilter.  Harnesses and CVE regressions compose property modules
to verify properties of real kernel source.

See the parent [`DESIGN.md`](../DESIGN.md) for the overall architecture
and milestones.  This file specifies only the conventions that every
module in this directory follows.

## Directory layout

```
properties/
├── README.md              (this file)
├── <primitive>/
│   ├── README.md          one page: what this module covers
│   ├── <primitive>.h      public API; contracts on all hook functions
│   ├── <primitive>.c      reference implementation; enforced against the
│   │                      contracts via goto-instrument --enforce-contract
│   ├── <primitive>.cocci  (later) Coccinelle prefilter: identifies
│   │                      functions that touch this primitive and are
│   │                      therefore worth running CBMC on
│   ├── test_enforce.c     proves <primitive>.c satisfies its contracts
│   ├── test_replace.c     proves that calling code inherits the
│   │                      obligations declared by the contracts
│   └── run.sh             runnable: exercises both tests via cbmc
└── ...
```

A module is "landed" when:

1. its contracts are committed in its directory;
2. its reference implementation is proved consistent with the
   contracts (`test_enforce.c` passes);
3. its propagation of obligations to callers is demonstrated
   (`test_replace.c` passes one "good" caller and rejects one "bad"
   caller);
4. at least one entry under `../cve-*` uses the module;
5. `run.sh` returns 0 from the repository root as well as from inside
   the module's directory.

## CBMC workflow

The tool chain has three stages.  An example, using the `page_provenance`
module, demonstrates all three:

```
# 1. compile sources (harness + reference implementation) into a single
#    goto binary
goto-cc -I properties/page_provenance              \
        harness.c properties/page_provenance/page_provenance.c \
        -o harness.gb

# 2. transform the goto binary with goto-instrument.  Either:
#
#    (a) --replace-call-with-contract <hook>      to abstract calls to
#        <hook> by its contract; callers then discharge the requires
#        clause at the call site;
#
#    (b) --dfcc <harness> --enforce-contract <hook>  to prove that the
#        body of <hook> satisfies its contract.  `--dfcc` activates
#        the dynamic frame-condition checking path, which handles
#        functions with loops (see
#        [`../CBMC_LIMITATIONS.md`](../CBMC_LIMITATIONS.md) entry
#        LIM-002 for why the non-DFCC path is not used).
goto-instrument --replace-call-with-contract write_to_page \
                harness.gb harness.abstracted.gb

# 3. verify
cbmc harness.abstracted.gb
```

`run.sh` scripts in each module do all three stages non-interactively.

## Ghost state

Many kernel properties depend on invariants that cannot be inferred
from the types of kernel objects alone — for example, "this page is
backed by a file the caller only has read permission to."  We track
such invariants through _ghost state_: additional metadata associated
with kernel objects, visible to CBMC but absent from a normal build.

Conventions:

- Ghost state lives in its own property module (e.g.
  `page_provenance/`).  Other modules depend on it rather than
  re-implementing it.
- The ghost state is accessed through module-provided accessor
  functions (e.g. `page_prov_of(struct page *)`).  Callers never
  touch the backing store directly.
- Ghost accessors are pure (no side effects beyond returning a value)
  and safe to call from `__CPROVER_requires`, `__CPROVER_ensures`,
  and assertions.
- Ghost mutators (e.g. `set_page_prov`) are called either by
  harnesses when they construct test objects, or by _annotated
  versions_ of kernel functions that bring objects into existence
  (e.g. `iov_iter_get_pages`, `grab_cache_page`).

Each module documents its own backing store implementation choice.
The reference choice (a small pointer-keyed side table) is used by
`page_provenance` and is acceptable for bounded harnesses.  Modules
that track many more objects may choose differently, as long as they
preserve the accessor semantics.

## Contract style

- Contracts are attached to function _declarations_ in the module's
  `.h`, so that any user of the header sees them.  The reference
  implementation matches the signature in the usual C way.
- `__CPROVER_requires` clauses are split into one clause per logical
  precondition, to produce one diagnostic per violation rather than a
  single combined failure.
- `__CPROVER_assigns` is written explicitly even when it is empty,
  both to document the intent and to silence `goto-instrument`'s
  defaulting.
- Use `__CPROVER_forall` / `__CPROVER_exists` for properties that
  range over collections (scatterlist entries, iovec segments).
- Predicates used in multiple contracts are defined as ordinary C
  functions in the header.  They must be pure.
- Write tests that exercise both the satisfying and the violating
  case for every non-trivial contract clause.

## Adding a new module

1. Create a subdirectory under `properties/`.
2. Define the kernel-facing API in `<name>.h` with contracts.
3. Provide `<name>.c` as a minimal reference implementation.
4. Write `test_enforce.c` and `test_replace.c`.
5. Write a `run.sh` that invokes `goto-cc`, `goto-instrument`, and
   `cbmc` in the three-stage chain above.
6. Write a `README.md` that describes the covered invariants, the
   kernel versions and subsystems the module has been validated
   against, and the CVE regressions (if any) that exercise it.
7. Tie the module to an entry under `../cve-*` to anchor the claim
   that it catches a real bug class.

A worked example is `page_provenance/` — the simplest real module.
Use it as a template.
