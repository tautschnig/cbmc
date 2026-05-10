/// \file
/// AEAD property module.
///
/// Models the subset of the Linux kernel's `struct aead_request` API
/// that the AF_ALG / `crypto/algif_aead.c` data path uses, and places
/// correct-use contracts on it.
///
/// The headline contract:
///
/// ```
/// void aead_request_set_crypt(req, src, dst, cryptlen, iv)
///   __CPROVER_requires(sgl_all_user_writable(dst));
/// ```
///
/// This expresses the invariant that any caller of the AEAD API must
/// ensure every page reachable through the destination scatterlist
/// (including chain-link traversals) is attacker-writable.  When a
/// caller chains page-cache pages into the destination — the shape of
/// CVE-2026-31431 ("Copy Fail") — `sgl_all_user_writable(dst)`
/// returns false and the contract's requires-clause fails at the call
/// site.
///
/// Depends on the `scatterlist` and `page_provenance` modules.
///
/// See the parent `properties/README.md` for module conventions.

#ifndef LINUX_PROPERTIES_AEAD_H
#define LINUX_PROPERTIES_AEAD_H

#include "../scatterlist/scatterlist.h"

/// Opaque forward declaration of the kernel's transform object.
/// Property-module code never looks inside.
struct crypto_aead;

/// Abbreviated model of `struct aead_request`.  Field layout follows
/// the kernel's where it matters for ordering in memory (so an
/// adapter layer, if ever needed, can remap by offset); DMA and async
/// members are omitted.
struct aead_request
{
  struct crypto_aead *tfm;
  unsigned int assoclen;
  unsigned int cryptlen;
  unsigned char *iv;
  struct scatterlist *src;
  struct scatterlist *dst;
};

/// Stash the transform handle on the request.  No security-relevant
/// precondition beyond `req` being non-NULL.
void aead_request_set_tfm(struct aead_request *req, struct crypto_aead *tfm)
  __CPROVER_requires(req != (struct aead_request *)0)
    __CPROVER_assigns(req->tfm);

/// Record the length of the associated-data prefix.  The relevant
/// invariants are on the length in relation to the scatterlist
/// contents; `_aead_recvmsg` computes `assoclen` from the socket's
/// `ctx->aead_assoclen` before the call.
void aead_request_set_ad(struct aead_request *req, unsigned int assoclen)
  __CPROVER_requires(req != (struct aead_request *)0)
    __CPROVER_assigns(req->assoclen);

/// Record the source and destination scatterlists, plaintext length,
/// and IV on the request.  This is the API call that the vulnerable
/// `_aead_recvmsg` invokes with `src == dst == rsgl`, where `rsgl`
/// has been chained into a page-cache-backed tx SGL.
///
/// The headline contract: the destination scatterlist must consist
/// entirely of pages the caller has write capability over.
void aead_request_set_crypt(
  struct aead_request *req,
  struct scatterlist *src,
  struct scatterlist *dst,
  unsigned int cryptlen,
  unsigned char *iv) __CPROVER_requires(req != (struct aead_request *)0)
  __CPROVER_requires(dst != (struct scatterlist *)0)
    __CPROVER_requires(sgl_all_user_writable(dst) == 1)
      __CPROVER_assigns(req->src, req->dst, req->cryptlen, req->iv);

#endif // LINUX_PROPERTIES_AEAD_H
