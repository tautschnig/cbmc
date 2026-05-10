/// \file
/// test_unit.c - assertion-based tests for the scatterlist property
/// module.
///
/// Covers the invariants a caller can rely on without invoking any
/// CBMC contract machinery:
///
/// 1. A freshly initialised SGL with one page tagged user-writable
///    has `sgl_all_user_writable` return 1.
///
/// 2. If any page in the chain is tagged `PAGE_CACHE_RO`, the
///    predicate returns 0.
///
/// 3. A chain that extends from one SGL into a second SGL whose pages
///    are page-cache is caught by the predicate: this is the exact
///    shape of CVE-2026-31431.
///
/// Run via run.sh.

#include "scatterlist.h"

#include "../test_support.h"

int main(void)
{
  // Backing pages.  CBMC treats these as distinct objects whose
  // addresses are stable pointers, suitable keys for the
  // page_provenance side table.
  struct page p_user, p_cache;

  struct scatterlist sgl1[4];
  struct scatterlist sgl2[4];

  // --------------------------------------------------------------
  // Case 1: one user-writable page, no chain.
  // --------------------------------------------------------------
  sg_init_table(sgl1, 4);
  sg_set_page(&sgl1[0], &p_user, 4096, 0);
  sgl1[0].end = 1;
  set_page_prov(&p_user, PAGE_USER_WRITABLE);

  __CPROVER_assert(
    sgl_all_user_writable(sgl1) == 1, "case 1: lone user-writable page passes");

  // --------------------------------------------------------------
  // Case 2: second page is page-cache.  Predicate returns false.
  // --------------------------------------------------------------
  sg_init_table(sgl1, 4);
  sg_set_page(&sgl1[0], &p_user, 4096, 0);
  sg_set_page(&sgl1[1], &p_cache, 4096, 0);
  sgl1[1].end = 1;
  set_page_prov(&p_user, PAGE_USER_WRITABLE);
  set_page_prov(&p_cache, PAGE_CACHE_RO);

  __CPROVER_assert(
    sgl_all_user_writable(sgl1) == 0, "case 2: page-cache entry detected");

  // --------------------------------------------------------------
  // Case 3: chain from sgl1 into sgl2, where sgl2 carries a
  // page-cache page.  This is the shape of CVE-2026-31431.
  // --------------------------------------------------------------
  sg_init_table(sgl1, 4);
  sg_init_table(sgl2, 4);

  sg_set_page(&sgl1[0], &p_user, 4096, 0);
  sg_set_page(&sgl2[0], &p_cache, 4096, 0);
  sgl2[0].end = 1;
  set_page_prov(&p_user, PAGE_USER_WRITABLE);
  set_page_prov(&p_cache, PAGE_CACHE_RO);

  // Make sgl1[0] a real entry and sgl1[1] a chain link into sgl2.
  sg_unmark_end(&sgl1[0]);
  sg_chain(sgl1, 2, sgl2);

  __CPROVER_assert(
    sgl_all_user_writable(sgl1) == 0,
    "case 3: chained page-cache entry detected (Copy Fail shape)");

  return 0;
}
