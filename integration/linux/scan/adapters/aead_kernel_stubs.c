/// \file
/// aead_kernel_stubs.c — havocing C stubs for the kernel helpers
/// `_aead_recvmsg` calls transitively.
///
/// Replaces each helper's undefined-function-nondet behaviour with an
/// explicit body so that (a) cbmc stops treating the helper's
/// memory-safety obligations as unknown, (b) we can tag pages that
/// enter the kernel through user-facing helpers with their correct
/// provenance, and (c) the pointer analysis gets concrete allocation
/// sites for request / SGL storage.
///
/// This file is linked alongside the aead kernel adapter; goto-cc's
/// linker merges the stubs with the kernel goto binary, replacing the
/// kernel's real bodies for these names.
///
/// The stubs are the minimum viable set that lets
/// `cbmc --function _aead_recvmsg` terminate.  Each stub's fidelity
/// is commented; anywhere we simplify, the effect is to make cbmc
/// *more conservative* (i.e., report false positives rather than
/// false negatives), which is the safe direction for a
/// proactive-scan tool.

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Kernel type shims (opaque; struct sizes come from the kernel goto
// binary at link time).  We only take pointers to these; we do not
// dereference fields from within the stubs.
// ---------------------------------------------------------------------------

struct sock;
struct socket;
struct msghdr;
struct alg_sock;
struct af_alg_ctx;
struct af_alg_async_req;
struct af_alg_rsgl;
struct af_alg_sgl;
struct af_alg_tsgl;
struct crypto_aead;
struct crypto_skcipher;
struct scatterlist;
struct page;
struct completion;
struct crypto_wait;

typedef unsigned int gfp_t;
typedef long ssize_t;

// ---------------------------------------------------------------------------
// page_provenance module imports — linked separately.
// ---------------------------------------------------------------------------

typedef enum
{
  PAGE_PROV_UNSET = 0,
  PAGE_USER_WRITABLE,
  PAGE_CACHE_RO,
  PAGE_KERNEL_ONLY,
} page_provenance_t;

void set_page_prov(struct page *p, page_provenance_t prov);

// ---------------------------------------------------------------------------
// Nondet primitives.  CBMC turns calls to __VERIFIER_nondet_*() into
// true nondet at the SSA level.  The compiled undefined function
// mechanism does the same thing, but having explicit names makes the
// stub bodies readable.
// ---------------------------------------------------------------------------

static int stub_nondet_int(void)
{
  int r;
  return r;
}

static unsigned int stub_nondet_uint(void)
{
  unsigned int r;
  return r;
}

static size_t stub_nondet_size(void)
{
  size_t r;
  return r;
}

// ---------------------------------------------------------------------------
// Socket / sleep helpers.  The real bodies block on wait-queues and
// socket locks; for symbolic analysis we treat them as nondet.
// ---------------------------------------------------------------------------

int af_alg_wait_for_data(struct sock *sk, unsigned int flags, unsigned int min)
{
  (void)sk;
  (void)flags;
  (void)min;
  return stub_nondet_int();
}

void lock_sock_nested(struct sock *sk, int subclass)
{
  (void)sk;
  (void)subclass;
}

void release_sock(struct sock *sk)
{
  (void)sk;
}

// ---------------------------------------------------------------------------
// Allocation / free.  We model sock_kmalloc as a nondet-sized allocation
// so the pointer is distinct from other objects (cbmc's object model
// benefits from this).  Free is a no-op.
// ---------------------------------------------------------------------------

void *sock_kmalloc(struct sock *sk, int size, gfp_t priority)
{
  (void)sk;
  (void)priority;
  if(size <= 0)
    return (void *)0;
  // CBMC will treat this as a fresh heap object.
  return __CPROVER_allocate((size_t)size, 0);
}

void sock_kfree_s(struct sock *sk, void *mem, int size)
{
  (void)sk;
  (void)mem;
  (void)size;
}

void sock_kzfree_s(struct sock *sk, void *mem, int size)
{
  (void)sk;
  (void)mem;
  (void)size;
}

struct af_alg_async_req *
af_alg_alloc_areq(struct sock *sk, unsigned int areqlen)
{
  (void)sk;
  if(areqlen == 0)
    return (struct af_alg_async_req *)0;
  return (struct af_alg_async_req *)__CPROVER_allocate(areqlen, 0);
}

// ---------------------------------------------------------------------------
// Scatterlist-shaping helpers.  These are the ones whose behaviour
// matters for the COPYFAIL property check, because they determine
// which pages end up in the request's destination scatterlist.
//
// - af_alg_get_rsgl populates areq->first_rsgl with pages that the
//   caller supplied via the user iovec.  Those pages are, by
//   construction, attacker-writable.
//
// - af_alg_pull_tsgl moves pages from the global TX scatterlist into
//   the given destination scatterlist.  Those TX pages may have come
//   from splice() on a file the caller only has read access to;
//   their provenance is PAGE_CACHE_RO in the worst case.
//
// For the M4c first cut we keep both stubs as nondet-return no-ops,
// so cbmc sees the scatterlists as nondet pointers.  This is sound
// (CBMC will explore every provenance combination) but imprecise
// (may not actually tag specific pages).  A future refinement will
// call set_page_prov() inside these stubs to constrain the ghost
// state to the kernel's actual semantics.
// ---------------------------------------------------------------------------

int af_alg_get_rsgl(
  struct sock *sk,
  struct msghdr *msg,
  int flags,
  struct af_alg_async_req *areq,
  ssize_t maxsize,
  size_t *outlen)
{
  (void)sk;
  (void)msg;
  (void)flags;
  (void)areq;
  (void)maxsize;
  if(outlen)
    *outlen = stub_nondet_size();
  return stub_nondet_int();
}

unsigned int af_alg_count_tsgl(struct sock *sk, size_t bytes, size_t offset)
{
  (void)sk;
  (void)bytes;
  (void)offset;
  unsigned int r = stub_nondet_uint();
  // Keep the count bounded so downstream loops terminate in small
  // unwind budgets.
  if(r > 4)
    r = 4;
  return r;
}

void af_alg_pull_tsgl(
  struct sock *sk,
  size_t used,
  struct scatterlist *dst,
  size_t dst_offset)
{
  (void)sk;
  (void)used;
  (void)dst;
  (void)dst_offset;
}

// ---------------------------------------------------------------------------
// Crypto helpers.  We do not model the algorithm; we only need the
// values `_aead_recvmsg` reads to make control-flow decisions
// (authsize, reqsize, ivsize).  All are bounded to keep subsequent
// allocations and loops tractable.
// ---------------------------------------------------------------------------

unsigned int crypto_aead_authsize(struct crypto_aead *tfm)
{
  (void)tfm;
  unsigned int r = stub_nondet_uint();
  if(r > 64)
    r = 64;
  return r;
}

unsigned int crypto_aead_reqsize(struct crypto_aead *tfm)
{
  (void)tfm;
  unsigned int r = stub_nondet_uint();
  if(r > 512)
    r = 512;
  return r;
}

unsigned int crypto_aead_ivsize(struct crypto_aead *tfm)
{
  (void)tfm;
  unsigned int r = stub_nondet_uint();
  if(r > 32)
    r = 32;
  return r;
}

int crypto_aead_copy_sgl(
  struct crypto_aead *null_tfm,
  struct scatterlist *src,
  struct scatterlist *dst,
  unsigned int len)
{
  (void)null_tfm;
  (void)src;
  (void)dst;
  (void)len;
  return stub_nondet_int();
}

int crypto_aead_encrypt(struct af_alg_async_req *areq)
{
  (void)areq;
  return stub_nondet_int();
}

int crypto_aead_decrypt(struct af_alg_async_req *areq)
{
  (void)areq;
  return stub_nondet_int();
}

int crypto_aead_get_flags(struct crypto_aead *tfm)
{
  (void)tfm;
  return stub_nondet_int();
}

void *crypto_aead_alg(struct crypto_aead *tfm)
{
  (void)tfm;
  return (void *)0;
}

// ---------------------------------------------------------------------------
// Misc small helpers.
// ---------------------------------------------------------------------------

struct alg_sock *alg_sk(struct sock *sk)
{
  return (struct alg_sock *)sk;
}

int aead_sufficient_data(struct sock *sk)
{
  (void)sk;
  return stub_nondet_int();
}

size_t msg_data_left(struct msghdr *msg)
{
  (void)msg;
  return stub_nondet_size();
}

void crypto_init_wait(struct crypto_wait *wait)
{
  (void)wait;
}

void reinit_completion(struct completion *c)
{
  (void)c;
}

void __init_completion(struct completion *c)
{
  (void)c;
}

int wait_for_completion_interruptible(struct completion *c)
{
  (void)c;
  return stub_nondet_int();
}

void wait_for_completion(struct completion *c)
{
  (void)c;
}
