/// \file
/// Reference implementation for the `scatterlist` property module.
///
/// Matches the public API in `scatterlist.h`.  The `sgl_all_user_writable`
/// predicate is the heart of the module: it folds over the chain
/// representation and queries the `page_provenance` ghost state.
///
/// The implementation uses bounded `for` loops so that CBMC's
/// unwinding is finite.  With `--unwinding-assertions`, CBMC flags
/// any harness whose scatterlists exceed `SG_CHAIN_MAX_STEPS`.  Such
/// harnesses should raise the bound at compile time.

#include "scatterlist.h"

void sg_init_table(struct scatterlist *sgl, unsigned int n)
{
  for(unsigned int i = 0; i < n; i++)
  {
    sgl[i].page = (struct page *)0;
    sgl[i].chain = (struct scatterlist *)0;
    sgl[i].offset = 0;
    sgl[i].length = 0;
    sgl[i].end = (i == n - 1);
  }
}

void sg_set_page(
  struct scatterlist *sg,
  struct page *page,
  unsigned int length,
  unsigned int offset)
{
  sg->page = page;
  sg->chain = (struct scatterlist *)0;
  sg->offset = offset;
  sg->length = length;
  // Do not touch `end` here; the caller controls it via
  // `sg_init_table` or `sg_mark_end`.
}

void sg_chain(
  struct scatterlist *prev,
  unsigned int nents,
  struct scatterlist *next)
{
  struct scatterlist *link = &prev[nents - 1];
  link->page = (struct page *)0;
  link->chain = next;
  link->end = 0;
  link->length = 0;
  link->offset = 0;
}

void sg_unmark_end(struct scatterlist *sg)
{
  sg->end = 0;
}

struct scatterlist *sg_next(struct scatterlist *sg)
{
  if(sg == (struct scatterlist *)0)
    return (struct scatterlist *)0;
  if(sg->end)
    return (struct scatterlist *)0;
  sg++;
  if(sg->chain != (struct scatterlist *)0)
    return sg->chain;
  return sg;
}

struct page *sg_page(struct scatterlist *sg)
{
  if(sg == (struct scatterlist *)0)
    return (struct page *)0;
  return sg->page;
}

int sgl_all_user_writable(struct scatterlist *sgl)
{
  struct scatterlist *sg = sgl;
  for(unsigned int i = 0; i < SG_CHAIN_MAX_STEPS; i++)
  {
    if(sg == (struct scatterlist *)0)
      return 1;
    if(sg->chain != (struct scatterlist *)0)
    {
      sg = sg->chain;
      continue;
    }
    if(
      sg->page != (struct page *)0 &&
      page_prov_of(sg->page) != PAGE_USER_WRITABLE)
      return 0;
    if(sg->end)
      return 1;
    sg = sg_next(sg);
  }
  // Bound exceeded: remain sound by reporting "cannot confirm."
  return 0;
}
