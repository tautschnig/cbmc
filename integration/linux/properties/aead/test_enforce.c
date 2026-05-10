/// \file
/// test_enforce.c - `--dfcc + --enforce-contract` tests for the aead
/// module.
///
/// DFCC's enforce mode requires a single top-level call to the
/// function under check, so this harness has one main() per enforced
/// function.  Selection is by -DENFORCE_<name> on the compile line;
/// the run.sh driver compiles and runs the appropriate one.
///
/// Each harness initialises fresh arguments nondeterministically,
/// establishes the contract's preconditions, and calls the target
/// function exactly once.  No assertions are placed in main(): the
/// contract's assigns/requires clauses are the property under check.
///
/// Note on aead_request_set_crypt: its requires clause contains the
/// predicate `sgl_all_user_writable(dst)`, which walks the
/// scatterlist chain.  The harness constructs a single user-writable
/// page so the predicate holds on entry.

#include "aead.h"

#include "../test_support.h"

#if defined(ENFORCE_AEAD_REQUEST_SET_TFM)
int main(void)
{
  struct aead_request req;
  struct crypto_aead *tfm;
  aead_request_set_tfm(&req, tfm);
  return 0;
}

#elif defined(ENFORCE_AEAD_REQUEST_SET_AD)
int main(void)
{
  struct aead_request req;
  unsigned int assoclen;
  aead_request_set_ad(&req, assoclen);
  return 0;
}

#elif defined(ENFORCE_AEAD_REQUEST_SET_CRYPT)
int main(void)
{
  // Build a user-writable SGL so the precondition
  // __CPROVER_requires(sgl_all_user_writable(dst) == 1) holds.
  struct page p;
  struct scatterlist dst[2];
  sg_init_table(dst, 2);
  sg_set_page(&dst[0], &p, 4096, 0);
  dst[0].end = 1;
  set_page_prov(&p, PAGE_USER_WRITABLE);

  struct scatterlist src[2];
  sg_init_table(src, 2);

  struct aead_request req;
  unsigned char iv[16];
  unsigned int cryptlen;

  aead_request_set_crypt(&req, src, dst, cryptlen, iv);
  return 0;
}

#else
#  error "Define exactly one ENFORCE_<name> on the compile line."
#endif
