/// \file
/// test_unit.c - assertion-based unit test for the page_provenance
/// reference implementation.
///
/// Proves that `set_page_prov(p, t)` followed by `page_prov_of(p)`
/// returns `t`, in the case where the backing table had room for the
/// store.  This is the core store/read-back invariant of the module.
///
/// This test does NOT attempt CBMC contract enforcement
/// (`--enforce-contract set_page_prov`): that would require loop
/// contracts on the linear search inside `set_page_prov`, and CBMC's
/// loop-contract support is brittle for loops that read from shared
/// memory (see `regression/contracts/quicksort_contracts_01` which is
/// labelled KNOWNBUG).  The assertion-based check here verifies the
/// same invariant in a form CBMC handles directly.
///
/// Run via run.sh.

#include "page_provenance.h"

int main(void)
{
  extern unsigned int __page_prov_table_size;

  struct page *p;
  __CPROVER_assume(p != (struct page *)0);

  page_provenance_t prov;
  __CPROVER_assume(
    prov == PAGE_USER_WRITABLE || prov == PAGE_CACHE_RO ||
    prov == PAGE_KERNEL_ONLY);

  unsigned int size_before = __page_prov_table_size;
  set_page_prov(p, prov);

  // If the table had room before the store, the read-back succeeds.
  // If it was already full and p was a new key, the store was
  // silently dropped and the readback can be PAGE_PROV_UNSET; the
  // assertion is guarded accordingly.
  __CPROVER_assert(
    size_before >= PAGE_PROV_TABLE_SIZE || page_prov_of(p) == prov,
    "page_provenance: store then read-back yields stored value");

  return 0;
}
