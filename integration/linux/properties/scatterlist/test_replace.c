/// \file
/// test_replace.c - `--replace-call-with-contract` test for a
/// hypothetical kernel API whose contract reads
/// `sgl_all_user_writable(dst)` in a requires clause.  Demonstrates
/// that the scatterlist predicate, when embedded in a contract and
/// then abstracted by goto-instrument at call sites, correctly
/// propagates obligations to callers.
///
/// A "good" caller constructs a destination SGL of user-writable
/// pages only.  A "bad" caller chains a page-cache page into the
/// destination.  With `--replace-call-with-contract write_sgl`, the
/// good caller's precondition holds and the bad caller's fails.
///
/// Run via run.sh.

#include "scatterlist.h"

#include "../test_support.h"

/// Hypothetical kernel API that writes through a destination
/// scatterlist.  Only the requires clause is material for this test;
/// the body is a no-op.
void write_sgl(struct scatterlist *dst)
  __CPROVER_requires(dst != (struct scatterlist *)0)
    __CPROVER_requires(sgl_all_user_writable(dst) == 1) __CPROVER_assigns();

void write_sgl(struct scatterlist *dst)
{
  (void)dst;
}

static void caller_good(struct page *p, struct scatterlist *sgl)
{
  sg_init_table(sgl, 2);
  sg_set_page(&sgl[0], p, 4096, 0);
  sgl[0].end = 1;
  set_page_prov(p, PAGE_USER_WRITABLE);
  write_sgl(sgl);
}

static void caller_bad(
  struct page *p_user,
  struct page *p_cache,
  struct scatterlist *sgl_rx,
  struct scatterlist *sgl_tx)
{
  // Destination starts as a user-writable single-entry list.
  sg_init_table(sgl_rx, 2);
  sg_init_table(sgl_tx, 2);
  sg_set_page(&sgl_rx[0], p_user, 4096, 0);
  sg_set_page(&sgl_tx[0], p_cache, 4096, 0);
  sgl_tx[0].end = 1;
  set_page_prov(p_user, PAGE_USER_WRITABLE);
  set_page_prov(p_cache, PAGE_CACHE_RO);

  // Chain in a page-cache page from sgl_tx.  This is the vulnerable
  // shape: source pages bleed into the destination.
  sg_unmark_end(&sgl_rx[0]);
  sg_chain(sgl_rx, 2, sgl_tx);

  write_sgl(sgl_rx);
}

int main(void)
{
  struct page p_user, p_cache;
  struct scatterlist sgl_a[2], sgl_b[2];

  int which;
  __CPROVER_assume(which == 0 || which == 1);

  if(which == 0)
    caller_good(&p_user, sgl_a);
  else
    caller_bad(&p_user, &p_cache, sgl_a, sgl_b);

  return 0;
}
