/// \file
/// aead_kernel_adapter_probe.c — vacuity-probe adapter for the aead
/// module.  Contract-compatible with aead_kernel_adapter.c but
/// substitutes a trivially-false `__CPROVER_requires(0 == 1)` for
/// the substantive `sgl_all_user_writable(dst) == 1` clause.
///
/// scan.py links this adapter INSTEAD OF the real one for a
/// vacuity-probe run: cbmc MUST report VERIFICATION FAILED on the
/// precondition assertion — that proves the contract call site is
/// reached on at least one path in the linked binary.  If cbmc
/// reports VERIFICATION SUCCESSFUL on the probe, the regular
/// verification is vacuous (call site unreachable) and the scan is
/// refused with `cbmc_status: "vacuity-risk"`.
///
/// This is the direct automation of the diagnostic technique that
/// unmasked LIM-009: the empty-body `$linkN`-suffixed kernel static
/// functions had been silently short-circuiting the scan to
/// SUCCESSFUL without ever reaching the target call site.

#include <stdint.h>

struct page;
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

// Trivially-false contracts, declared on both the external and the
// file-local-mangled names so that `--replace-call-with-contract`
// finds one of them regardless of how the kernel source happens to
// emit the call.

void __CPROVER_file_local_aead_h_aead_request_set_crypt(
  struct aead_request *req,
  struct scatterlist *src,
  struct scatterlist *dst,
  unsigned int cryptlen,
  u8 *iv) __CPROVER_requires(0 == 1) __CPROVER_assigns();

void aead_request_set_crypt(
  struct aead_request *req,
  struct scatterlist *src,
  struct scatterlist *dst,
  unsigned int cryptlen,
  u8 *iv) __CPROVER_requires(0 == 1) __CPROVER_assigns();
