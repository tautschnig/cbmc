/// \file
/// test_replace.c - `--replace-call-with-contract` tests for contracts
/// that read `page_prov_of` in a requires clause.
///
/// Declares a hypothetical kernel API `write_to_page(p)` whose contract
/// insists that `p` is user-writable.  A "good" caller tags the page
/// PAGE_USER_WRITABLE before the call; a "bad" caller tags it
/// PAGE_CACHE_RO.  When run through `goto-instrument
/// --replace-call-with-contract write_to_page`, the good caller
/// verifies and the bad caller fails the requires clause.
///
/// This exercises the plumbing: compilation of a contract that uses the
/// module's ghost getter, propagation of obligations to call sites, and
/// production of a diagnostic that names the violated requires clause
/// (not just "some assertion failed").
///
/// Run via run.sh.

#include "page_provenance.h"

/// Hypothetical kernel API with a page-provenance precondition.
/// The body is a no-op because we are exercising the contract
/// machinery, not the semantics of writes.
void write_to_page(struct page *p) __CPROVER_requires(p != (struct page *)0)
  __CPROVER_requires(page_prov_of(p) == PAGE_USER_WRITABLE) __CPROVER_assigns();

void write_to_page(struct page *p)
{
  (void)p;
}

/// "Good" caller: tags a fresh page PAGE_USER_WRITABLE and calls the
/// API.  The requires clause holds.
static void caller_good(struct page *p)
{
  set_page_prov(p, PAGE_USER_WRITABLE);
  write_to_page(p);
}

/// "Bad" caller: tags a page PAGE_CACHE_RO and calls the API anyway.
/// The requires clause is violated.
static void caller_bad(struct page *p)
{
  set_page_prov(p, PAGE_CACHE_RO);
  write_to_page(p);
}

int main(void)
{
  struct page *p;
  __CPROVER_assume(p != (struct page *)0);

  int which;
  __CPROVER_assume(which == 0 || which == 1);

  if(which == 0)
    caller_good(p);
  else
    caller_bad(p);

  return 0;
}
