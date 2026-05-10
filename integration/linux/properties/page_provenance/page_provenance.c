/// \file
/// Reference implementation for the `page_provenance` module.
///
/// The backing store is a small pointer-keyed side table with
/// fixed-length linear search.  The table size
/// (`PAGE_PROV_TABLE_SIZE`) is bounded so CBMC's unwinding is finite,
/// and is deliberately small for fast verification of tiny harnesses.
/// Harnesses that construct more than `PAGE_PROV_TABLE_SIZE` pages
/// must raise the capacity with `-DPAGE_PROV_TABLE_SIZE=<n>` when
/// compiling.
///
/// The implementation is linear-search, not a hash table, for two
/// reasons: (1) it keeps the control flow obvious to CBMC, and
/// (2) `struct page *` has no ordering or hash that CBMC already
/// understands.  A production deployment may want a better backing
/// store; the interface tolerates that.

#include "page_provenance.h"

struct __page_prov_entry __page_prov_table[PAGE_PROV_TABLE_SIZE];
unsigned int __page_prov_table_size;

page_provenance_t page_prov_of(struct page *p)
{
  for(unsigned int i = 0; i < PAGE_PROV_TABLE_SIZE; i++)
  {
    if(i >= __page_prov_table_size)
      break;
    if(__page_prov_table[i].page == p)
      return __page_prov_table[i].prov;
  }
  return PAGE_PROV_UNSET;
}

void set_page_prov(struct page *p, page_provenance_t prov)
{
  // Update-in-place if the page is already tagged.
  for(unsigned int i = 0; i < PAGE_PROV_TABLE_SIZE; i++)
  {
    if(i >= __page_prov_table_size)
      break;
    if(__page_prov_table[i].page == p)
    {
      __page_prov_table[i].prov = prov;
      return;
    }
  }
  // Otherwise append, or silently drop if the table is full.
  if(__page_prov_table_size < PAGE_PROV_TABLE_SIZE)
  {
    __page_prov_table[__page_prov_table_size].page = p;
    __page_prov_table[__page_prov_table_size].prov = prov;
    __page_prov_table_size++;
  }
}
