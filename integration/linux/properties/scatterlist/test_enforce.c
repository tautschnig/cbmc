/// \file
/// test_enforce.c - `--dfcc + --enforce-contract` tests for the
/// scatterlist module.
///
/// DFCC's enforce mode requires a single top-level call to the
/// function under check, so this harness has one main() per enforced
/// function.  Which one runs is selected by -DENFORCE_<name> on the
/// compile line; the run.sh driver compiles and runs the appropriate
/// one.
///
/// Each harness initialises fresh arguments nondeterministically,
/// subject to preconditions the contract requires, and calls the
/// target function exactly once.  No assertion is placed in main():
/// the contract's assigns clause is the property being checked.

#include "scatterlist.h"

#include "../test_support.h"

#if defined(ENFORCE_SG_INIT_TABLE)
int main(void)
{
  // A fixed size keeps DFCC tractable; the linear sg_init_table loop
  // is bounded by this constant.
  struct scatterlist sgl[4];
  sg_init_table(sgl, 4);
  return 0;
}

#elif defined(ENFORCE_SG_SET_PAGE)
int main(void)
{
  struct scatterlist sg;
  struct page p;
  unsigned int length;
  unsigned int offset;
  sg_set_page(&sg, &p, length, offset);
  return 0;
}

#elif defined(ENFORCE_SG_CHAIN)
int main(void)
{
  struct scatterlist prev[4];
  struct scatterlist next[4];
  unsigned int nents;
  __CPROVER_assume(nents > 0 && nents <= 4);
  sg_chain(prev, nents, next);
  return 0;
}

#elif defined(ENFORCE_SG_UNMARK_END)
int main(void)
{
  struct scatterlist sg;
  sg_unmark_end(&sg);
  return 0;
}

#else
#  error "Define exactly one ENFORCE_<name> on the compile line."
#endif
