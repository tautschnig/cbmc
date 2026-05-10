/// \file
/// Reference implementation for the `aead` property module.
///
/// The implementation is the trivial one: each function records the
/// argument(s) on the request structure exactly as the kernel's own
/// do, because the kernel's versions are likewise trivial recorders
/// (the real crypto work happens in a later `crypto_aead_encrypt`
/// call via an algorithm-specific `.encrypt` dispatch).  The point
/// of this module is not to execute crypto; it is to express the
/// correct-use contract on the setters so that CBMC checks it at
/// every call site.

#include "aead.h"

void aead_request_set_tfm(struct aead_request *req, struct crypto_aead *tfm)
{
  req->tfm = tfm;
}

void aead_request_set_ad(struct aead_request *req, unsigned int assoclen)
{
  req->assoclen = assoclen;
}

void aead_request_set_crypt(
  struct aead_request *req,
  struct scatterlist *src,
  struct scatterlist *dst,
  unsigned int cryptlen,
  unsigned char *iv)
{
  req->src = src;
  req->dst = dst;
  req->cryptlen = cryptlen;
  req->iv = iv;
}
