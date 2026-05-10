# `aead` property module

Expresses the correct-use protocol of the kernel's AEAD API as CBMC
function contracts.  The headline contract is on
`aead_request_set_crypt`:

```c
void aead_request_set_crypt(req, src, dst, cryptlen, iv)
  __CPROVER_requires(sgl_all_user_writable(dst) == 1);
```

Any kernel function that can reach `aead_request_set_crypt` with a
destination scatterlist that chains into pages the caller does not
have write capability over fails this contract at the call site.
That is the exact shape of CVE-2026-31431 ("Copy Fail"), and the
`test_copyfail.c` regression confirms the modules catch it.

Depends on `scatterlist` and (transitively) on `page_provenance`.

See the parent [`README.md`](../README.md) for module conventions.

## API

```c
void aead_request_set_tfm  (struct aead_request *req,
                            struct crypto_aead *tfm);
void aead_request_set_ad   (struct aead_request *req,
                            unsigned int assoclen);
void aead_request_set_crypt(struct aead_request *req,
                            struct scatterlist *src,
                            struct scatterlist *dst,
                            unsigned int cryptlen,
                            unsigned char *iv);
```

The contracts live in `aead.h`; see that header for the full
requires/assigns clauses.  The module deliberately does not model
`crypto_aead_encrypt` / `_decrypt` — those dispatch through an
algorithm-specific `.encrypt` pointer and have no single
protocol-level contract.  Correctness for them lives in the
algorithm implementation, not here.

## Tests

- `test_copyfail.c` — a stripped-down model of `_aead_recvmsg`
  exercising the CVE-2026-31431 shape.  Compiled twice: the default
  build reproduces the vulnerable control flow
  (`sg_chain(rx, _, tx); set_crypt(req, rx, rx, ...)`); building
  with `-DFIXED` reproduces the fixed control flow
  (`set_crypt(req, tx, rx, ...)`, no chain).  Both are transformed
  with `goto-instrument --replace-call-with-contract
  aead_request_set_crypt` before `cbmc` runs.
  - vulnerable → `VERIFICATION FAILED` with the `aead_request_set_crypt`
    precondition failing;
  - fixed → `VERIFICATION SUCCESSFUL`.

This is the property-module-based counterpart to the abstract-model
regression in `../../cve-2026-31431/`.  Both remain in the tree: the
abstract model is a minimal demonstration of the assertion-based
approach; `test_copyfail.c` shows the same bug caught through
contracts, which is the mechanism that will scale to real kernel
source.

## Known gaps

- No contract captures the kernel's own invariant that AEAD
  algorithms with `crypto_aead_authsize > 0` need `cryptlen >=
  authsize` to make sense.  A more complete contract would add
  `__CPROVER_requires(cryptlen >= authsize)` once `authsize` can be
  read; this is future work.

## Versions

Validated against CBMC 6.9.0 (`build/bin/cbmc`).  Does not require
kernel headers.
