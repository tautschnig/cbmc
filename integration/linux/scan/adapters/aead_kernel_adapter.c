/// \file
/// aead_kernel_adapter.c — bridges the aead property module's
/// contract onto the Linux kernel's real `aead_request_set_crypt`.
///
/// When linked with a goto-cc binary of a kernel .c file (e.g.
/// `crypto/algif_aead.c`) and run through
///
///   goto-instrument --replace-call-with-contract aead_request_set_crypt
///
/// this adapter attaches the `__CPROVER_requires(sgl_all_user_writable(dst))`
/// contract to the kernel's `aead_request_set_crypt` call sites.
/// Because kernel `struct scatterlist` bit-packs chain/end markers into
/// `page_link`, this file re-implements the predicate directly against
/// that layout (matching `include/linux/scatterlist.h` verbatim),
/// rather than importing the property module's abstract
/// `struct scatterlist` which uses explicit `chain` / `end` fields.
///
/// Page provenance is still tracked through the
/// `properties/page_provenance` module's side table: that module does
/// not require any particular `struct page` layout and is reusable
/// here unchanged.

#include <stdint.h>

// ---------------------------------------------------------------------------
// Kernel type declarations.  Full definitions come from the kernel goto
// binary at link time; we only need enough shape to take pointers and
// access `page_link` on `struct scatterlist`.
// ---------------------------------------------------------------------------

struct page;
struct aead_request;

// Must match include/linux/scatterlist.h on the kernel version we scan.
// The structurally relevant field is `page_link`, which bit-packs:
//
//   bit 0 (SG_CHAIN) — this entry is a chain link; bits 2+ point to the
//   next scatterlist array.
//   bit 1 (SG_END)   — this is the last entry of a (sub-)array.
//   bits 2.. — the page pointer or chain pointer, aligned so bits 0/1
//   are always free for the markers.
//
// For x86_64 allnoconfig (and the linux_5_10 tree this adapter has been
// validated against), the sibling fields beyond `page_link` are not
// touched by our contract predicate; they are present only so the
// struct layout matches the kernel goto binary.
struct scatterlist
{
  uintptr_t page_link;
  unsigned int offset;
  unsigned int length;
  uintptr_t dma_address;
  unsigned int dma_length;
};

typedef unsigned char u8;

// ---------------------------------------------------------------------------
// page_provenance ghost state — API provided by
// properties/page_provenance/page_provenance.c, which can be linked
// together with this adapter.  No struct-layout dependency.
// ---------------------------------------------------------------------------

typedef enum
{
  PAGE_PROV_UNSET = 0,
  PAGE_USER_WRITABLE,
  PAGE_CACHE_RO,
  PAGE_KERNEL_ONLY,
} page_provenance_t;

page_provenance_t page_prov_of(struct page *p);

// ---------------------------------------------------------------------------
// Scatterlist walkers, matching the kernel's bit-packed encoding.
// ---------------------------------------------------------------------------

#define SG_CHAIN ((uintptr_t)1 << 0)
#define SG_END ((uintptr_t)1 << 1)
#define SG_LINK_MASK (~(SG_CHAIN | SG_END))

#ifndef SG_CHAIN_MAX_STEPS
#  define SG_CHAIN_MAX_STEPS 16
#endif

static struct scatterlist *k_sg_next(struct scatterlist *sg)
{
  if(sg->page_link & SG_END)
    return (struct scatterlist *)0;
  sg++;
  if(sg->page_link & SG_CHAIN)
    return (struct scatterlist *)(sg->page_link & SG_LINK_MASK);
  return sg;
}

static struct page *k_sg_page(struct scatterlist *sg)
{
  if(sg->page_link & SG_CHAIN)
    return (struct page *)0;
  return (struct page *)(sg->page_link & SG_LINK_MASK);
}

// ---------------------------------------------------------------------------
// Predicate: every page reachable from `sgl` must be user-writable.
// Pure, safe to call inside __CPROVER_requires / __CPROVER_assert.
// ---------------------------------------------------------------------------

int sgl_all_user_writable(struct scatterlist *sgl)
{
  struct scatterlist *sg = sgl;
  for(unsigned int i = 0; i < SG_CHAIN_MAX_STEPS; i++)
  {
    if(sg == (struct scatterlist *)0)
      return 1;
    struct page *p = k_sg_page(sg);
    if(p != (struct page *)0 && page_prov_of(p) != PAGE_USER_WRITABLE)
      return 0;
    sg = k_sg_next(sg);
  }
  // Bound exceeded: remain sound by reporting "cannot confirm."
  return 0;
}

// ---------------------------------------------------------------------------
// The contract itself.  Attached to a declaration (no body here — the
// kernel binary supplies the body; goto-instrument will replace the
// call with this contract).
// ---------------------------------------------------------------------------

void aead_request_set_crypt(
  struct aead_request *req,
  struct scatterlist *src,
  struct scatterlist *dst,
  unsigned int cryptlen,
  u8 *iv) __CPROVER_requires(req != (struct aead_request *)0)
  __CPROVER_requires(dst != (struct scatterlist *)0)
    __CPROVER_requires(sgl_all_user_writable(dst) == 1) __CPROVER_assigns();
