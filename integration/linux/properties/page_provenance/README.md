# `page_provenance` property module

Ghost-state primitive that associates a `page_provenance_t` tag with a
kernel `struct page`, readable from CBMC contracts and assertions.
Other property modules (`scatterlist`, `aead`, ...) depend on it to
express page-level capability invariants.

See the parent [`README.md`](../README.md) for module conventions.

## API

```c
typedef enum {
    PAGE_PROV_UNSET,     // default (no annotation)
    PAGE_USER_WRITABLE,  // user iovec memory
    PAGE_CACHE_RO,       // page cache of a read-only-to-caller file
    PAGE_KERNEL_ONLY,    // internal kernel memory
} page_provenance_t;

page_provenance_t page_prov_of(struct page *p);
void              set_page_prov(struct page *p, page_provenance_t);
```

`page_prov_of` is pure and callable from `__CPROVER_requires`,
`__CPROVER_ensures`, and `__CPROVER_assert`.  `set_page_prov` mutates
a side table; it is intended to be called either from harnesses
during setup or from annotated versions of kernel memory-management
helpers (e.g. `iov_iter_get_pages`, `grab_cache_page_nowait`) when
those are wired up in a later milestone.

## Backing store

A fixed-capacity pointer-keyed side table
(`__page_prov_table[PAGE_PROV_TABLE_SIZE]`).  Default capacity is
16; raise it via `-DPAGE_PROV_TABLE_SIZE=<n>` at compile time if a
harness constructs more pages than that.  Overflow is silent; pages
beyond the capacity read back as `PAGE_PROV_UNSET`.  The capacity can
only be set, not queried at runtime, to keep the implementation
CBMC-friendly.

## Tests

- `test_unit.c` — proves the store/read-back invariant:
  `set_page_prov(p, t)` followed by `page_prov_of(p)` returns `t`
  provided the table had room.  Run with plain `cbmc`.
- `test_replace.c` — declares a hypothetical kernel API
  `write_to_page(p)` with a requires clause reading
  `page_prov_of(p) == PAGE_USER_WRITABLE`, and two callers that
  respectively satisfy and violate the clause.  Run via
  `goto-instrument --replace-call-with-contract write_to_page` +
  `cbmc`; the bad caller's precondition is expected to FAIL with a
  `precondition ... write_to_page ... caller_bad ... FAILURE`
  diagnostic.

Both tests are exercised by `run.sh`.

## Known gap

None currently: `set_page_prov`'s contract is enforced via
`goto-instrument --dfcc main --enforce-contract set_page_prov`; see
the `enforce` case in `run.sh`.  DFCC's only constraint is that the
enforce-mode harness makes a single top-level call to the function
under check, which `test_unit.c` already satisfies.

## Versions

Validated against CBMC 6.9.0 (`build/bin/cbmc`).  No kernel headers
are required to build or run the module on its own.

## Downstream users

None yet.  The `aead` module planned for milestone M3 will use
`page_prov_of` inside the requires clause of
`aead_request_set_crypt`; the CVE-2026-31431 regression will use the
combined module against the real `crypto/algif_aead.c` source.
