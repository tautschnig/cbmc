/// \file
/// aead_kernel_direct_harness.c — direct-call scan harness for the
/// aead property module.
///
/// ## Why this exists
///
/// The earlier `aead_kernel_harness.c` routed the scan through the
/// full `_aead_recvmsg` body, intending for CBMC to autonomously
/// synthesise a vulnerable scatterlist shape from the kernel
/// control flow.  LIM-012 in CBMC_LIMITATIONS.md documents the
/// finding that this path is non-monotonically sensitive to the
/// `slice_preserve` list: the scan's FAILED verdict on the
/// vulnerable kernel was partly driven by nondet-return stubs
/// polluting `first_rsgl.sgl.sg` with nondet `page_link` bits,
/// not exclusively by the Copy Fail scatterlist shape.
///
/// This harness takes a different tack: it constructs a
/// kernel-layout scatterlist explicitly (vulnerable or safe,
/// selected by `#ifdef FIXED`) and calls the contract target
/// directly.  The scan's job then shrinks to:
///
///   1. confirm the kernel source compiles (`compile_file.sh`);
///   2. confirm the mangled contract name resolves in the linked
///      binary (the `required_bodies` + vacuity-probe guardrails);
///   3. confirm the contract correctly accepts a safe shape and
///      rejects a vulnerable shape (cbmc on this harness).
///
/// This gives up "CBMC synthesises the vulnerable input from the
/// kernel body" — which the Coccinelle prefilter already covers
/// textually and which LIM-012 showed we cannot deliver soundly
/// under the current pipeline — in exchange for a predictable,
/// end-to-end-sound verification of the property module against
/// kernel-layout inputs.
///
/// ## Usage
///
/// Compiled twice by scan.py: once without `-DFIXED` (vulnerable
/// shape, expect FAILED on the contract precondition) and once
/// with (safe shape, expect SUCCESSFUL).  Both runs link against
/// the same kernel binary, adapter, stubs, and page_provenance
/// module.

// Compiled under the kernel's `-nostdinc` regime, so we cannot
// `#include <stdint.h>` or `<stddef.h>`; define the scalar types
// locally to match the kernel's ABI on x86_64.
typedef unsigned long uintptr_t;
typedef unsigned long size_t;

// Match the kernel's <linux/scatterlist.h> bit encoding.  Kept
// local (not #include) so the harness is independent of kernel
// version drift for this reduction.
struct page
{
  unsigned long _pad;
};
struct aead_request;
struct scatterlist
{
  uintptr_t page_link;
  unsigned int offset;
  unsigned int length;
  uintptr_t dma_address;
  unsigned int dma_length;
};
typedef unsigned char u8;

#define SG_CHAIN ((uintptr_t)1 << 0)
#define SG_END ((uintptr_t)1 << 1)

// page_provenance module imports.
typedef enum
{
  PAGE_PROV_UNSET = 0,
  PAGE_USER_WRITABLE,
  PAGE_CACHE_RO,
  PAGE_KERNEL_ONLY,
} page_provenance_t;

void set_page_prov(struct page *p, page_provenance_t prov);

// Contract targets declared by the adapter.  scan.py applies
// --replace-call-with-contract to both the external and
// file-local-mangled forms; we call the external here and rely on
// goto-cc to unify it with the adapter's contract declaration.
void aead_request_set_crypt(
  struct aead_request *req,
  struct scatterlist *src,
  struct scatterlist *dst,
  unsigned int cryptlen,
  u8 *iv);

int main(void)
{
  // Backing pages — addresses are what the ghost table keys on,
  // the kernel's struct page layout is irrelevant here.
  struct page user_page, cache_page;
  set_page_prov(&user_page, PAGE_USER_WRITABLE);
  set_page_prov(&cache_page, PAGE_CACHE_RO);

  // TX SGL — one entry pointing at a page-cache page, end marked.
  struct scatterlist tsgl[2];
  tsgl[0].page_link = (uintptr_t)&cache_page | SG_END;
  tsgl[0].offset = 0;
  tsgl[0].length = 4096;
  tsgl[0].dma_address = 0;
  tsgl[0].dma_length = 0;

  // RX SGL — one entry of user page, room for a chain link.
  struct scatterlist rsgl[2];
  rsgl[0].page_link = (uintptr_t)&user_page;
  rsgl[0].offset = 0;
  rsgl[0].length = 4096;
  rsgl[0].dma_address = 0;
  rsgl[0].dma_length = 0;

#ifndef FIXED
  // ---------- Vulnerable shape ----------
  //
  // Mimics what `_aead_recvmsg`'s decrypt-with-chain branch does
  // in Linux 5.10:
  //
  //     sg_unmark_end(sgl_prev->sg + sgl_prev->npages - 1);
  //     sg_chain(sgl_prev->sg, sgl_prev->npages + 1, areq->tsgl);
  //     aead_request_set_crypt(req, rsgl_src, rsgl_src, used, ctx->iv);
  //
  // Inlined: rsgl[0] loses SG_END (we never set it), rsgl[1] is
  // a chain link into tsgl.
  rsgl[1].page_link = (uintptr_t)tsgl | SG_CHAIN;
  rsgl[1].offset = 0;
  rsgl[1].length = 0;
  rsgl[1].dma_address = 0;
  rsgl[1].dma_length = 0;

  unsigned char req_buf[256];
  u8 iv[16];
  aead_request_set_crypt((struct aead_request *)req_buf, rsgl, rsgl, 4096, iv);
#else
  // ---------- Fixed shape ----------
  //
  // The upstream fix scheme: separate src and dst SGLs rather
  // than an in-place chain.  Here we pass `tsgl` as src and mark
  // `rsgl[0]` as SG_END so the walker terminates cleanly on the
  // destination.
  rsgl[0].page_link |= SG_END;

  unsigned char req_buf[256];
  u8 iv[16];
  aead_request_set_crypt((struct aead_request *)req_buf, tsgl, rsgl, 4096, iv);
#endif

  return 0;
}
