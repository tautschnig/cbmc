/// \file
/// test_copyfail.c - proves that the composed
/// page_provenance + scatterlist + aead property modules catch the
/// CVE-2026-31431 ("Copy Fail") control-flow shape.
///
/// The harness is structurally a stripped-down `_aead_recvmsg` from
/// the vulnerable `crypto/algif_aead.c`:
///
/// - A user-writable rx scatterlist (from a user iovec) is built.
/// - A page-cache-backed tx scatterlist (from splice) is built.
/// - The rx SGL is chained into the tx SGL (sg_unmark_end + sg_chain).
/// - aead_request_set_crypt is called with src == dst == rx.
///
/// The aead module's __CPROVER_requires clause on
/// aead_request_set_crypt calls sgl_all_user_writable(dst).  Because
/// dst == rx, and rx's chain extends into a page-cache page, the
/// predicate returns false and the contract fails at the call site.
/// This is exactly what a pre-merge scan of the real
/// `crypto/algif_aead.c` with --replace-call-with-contract
/// aead_request_set_crypt would report.
///
/// The fixed variant of the same function uses separate src and dst
/// SGLs and never calls sg_chain(rx, _, tx); the same harness,
/// rebuilt with `FIXED` defined, satisfies the contract.
///
/// Run via run.sh.

#include "aead.h"

#include "../test_support.h"

int main(void)
{
  // Backing pages.  `p_user` goes into the rx SGL; `p_cache` is the
  // page that splice() could have brought in from a file the caller
  // only had read permission to.
  struct page p_user, p_cache;
  set_page_prov(&p_user, PAGE_USER_WRITABLE);
  set_page_prov(&p_cache, PAGE_CACHE_RO);

  // Build the scatterlists.
  struct scatterlist rsgl[2];
  struct scatterlist tsgl[2];

  sg_init_table(rsgl, 2);
  sg_init_table(tsgl, 2);

  sg_set_page(&rsgl[0], &p_user, 4096, 0);
  rsgl[0].end = 1;

  sg_set_page(&tsgl[0], &p_cache, 4096, 0);
  tsgl[0].end = 1;

#ifndef FIXED
  // -------------------------- Vulnerable variant --------------------------
  // This mirrors the decrypt path of `_aead_recvmsg` before commit
  // a664bf3d603d:
  //
  //     sg_unmark_end(&rsgl[last]);
  //     sg_chain(rsgl, nents + 1, areq->tsgl);
  //     aead_request_set_crypt(req, rsgl, rsgl, used, ctx->iv);
  //
  // src == dst == rsgl, and rsgl is chained into page-cache memory.
  sg_unmark_end(&rsgl[0]);
  sg_chain(rsgl, 2, tsgl);

  struct aead_request req;
  unsigned char iv[16];
  aead_request_set_crypt(&req, rsgl, rsgl, 4096, iv);
#else
  // ----------------------------- Fixed variant ----------------------------
  // Mirrors commit a664bf3d603d:
  //
  //     aead_request_set_crypt(req, tsgl_src, rsgl, used, ctx->iv);
  //
  // src and dst are distinct; no sg_chain merges source pages into
  // the destination.
  struct aead_request req;
  unsigned char iv[16];
  aead_request_set_crypt(&req, tsgl, rsgl, 4096, iv);
#endif

  return 0;
}
