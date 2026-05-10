/// \file
/// scatterlist property module.
///
/// Models the subset of the Linux kernel's `struct scatterlist` API
/// that the `crypto/algif_aead.c` data path uses: scatterlists are
/// arrays of entries, some of which are "chain links" that redirect
/// to another array, terminated by an "end" marker.  The
/// `sgl_all_user_writable` predicate folds over this structure and
/// checks that every reachable page is tagged
/// `PAGE_USER_WRITABLE` in the `page_provenance` module's ghost
/// state.
///
/// Intentional simplifications relative to the kernel:
///
/// - The kernel encodes chain/end markers in the low bits of the
///   `page_link` field of `struct scatterlist`.  This header uses
///   explicit `chain` and `end` members, which avoids bit-twiddling
///   inside contract predicates.  When this module is linked against
///   real kernel source, either (a) the harness substitutes these
///   definitions for the kernel's via `-include` ordering, or (b) a
///   thin translation layer adapts the two representations.  Both
///   approaches are within scope for milestone M3 and later.
///
/// - The kernel's `sg_next` returns NULL at the end; we match that
///   semantics.
///
/// - No DMA-specific members (`dma_address`, `dma_length`,
///   `dma_flags`) are modelled.
///
/// See the parent `properties/README.md` for module conventions.

#ifndef LINUX_PROPERTIES_SCATTERLIST_H
#define LINUX_PROPERTIES_SCATTERLIST_H

#include "../page_provenance/page_provenance.h"

/// Upper bound on chain traversal.  Contracts and predicates that
/// fold over a scatterlist stop after this many steps, conservatively
/// returning `false` on overflow.  Harnesses that build deeper chains
/// must raise this value with `-DSG_CHAIN_MAX_STEPS=<n>`.
#ifndef SG_CHAIN_MAX_STEPS
#  define SG_CHAIN_MAX_STEPS 16
#endif

/// A single scatterlist entry.  Name layout is ABI-compatible with the
/// kernel's where practical, but `chain` and `end` are explicit
/// instead of packed into the low bits of `page_link`.
struct scatterlist
{
  struct page *page;         ///< target page; NULL on chain links
  struct scatterlist *chain; ///< non-NULL ⇒ this entry is a link
  unsigned int offset;       ///< byte offset inside the page
  unsigned int length;       ///< byte count from that offset
  _Bool end;                 ///< true ⇒ last entry of this SGL
};

/// Initialise `n` scatterlist entries to the empty/cleared state and
/// mark the final one as `end`.
void sg_init_table(struct scatterlist *sgl, unsigned int n)
  __CPROVER_requires(sgl != (struct scatterlist *)0) __CPROVER_requires(n > 0)
    __CPROVER_assigns(__CPROVER_object_whole(sgl));

/// Attach a page to an SGL entry with a length/offset pair.
void sg_set_page(
  struct scatterlist *sg,
  struct page *page,
  unsigned int length,
  unsigned int offset) __CPROVER_requires(sg != (struct scatterlist *)0)
  __CPROVER_assigns(*sg);

/// Convert the `nents-1`'th entry of `prev` into a chain link pointing
/// at `next`.  Matches `sg_chain` in `linux/scatterlist.h` semantically.
void sg_chain(
  struct scatterlist *prev,
  unsigned int nents,
  struct scatterlist *next) __CPROVER_requires(prev != (struct scatterlist *)0)
  __CPROVER_requires(nents > 0)
    __CPROVER_requires(next != (struct scatterlist *)0)
      __CPROVER_assigns(prev[nents - 1]);

/// Clear the end marker on `sg`.  Used immediately before `sg_chain`
/// on the same entry.
void sg_unmark_end(struct scatterlist *sg)
  __CPROVER_requires(sg != (struct scatterlist *)0) __CPROVER_assigns(sg->end);

/// Pure: return the next entry in the scatterlist (following a chain
/// link if present) or NULL at the end.  Safe to call from contract
/// predicates and assertions.
struct scatterlist *sg_next(struct scatterlist *sg) __CPROVER_assigns();

/// Pure: return the page backing `sg`, or NULL if the entry has no
/// page attached (e.g. a bare chain link).  Matches `sg_page` in
/// `linux/scatterlist.h` semantically.
struct page *sg_page(struct scatterlist *sg) __CPROVER_assigns();

/// Pure: true iff every page reachable from `sgl` via `sg_next` has
/// provenance `PAGE_USER_WRITABLE`.  Bounds traversal at
/// `SG_CHAIN_MAX_STEPS`; returns false if the bound is exceeded, to
/// remain sound under deeper chains than the harness configured for.
/// Safe to call from contract predicates and assertions.
///
/// This is the predicate that turns the `page_provenance` ghost state
/// into a usable precondition for kernel APIs that write through a
/// destination scatterlist.
_Bool sgl_all_user_writable(struct scatterlist *sgl) __CPROVER_assigns();

#endif // LINUX_PROPERTIES_SCATTERLIST_H
