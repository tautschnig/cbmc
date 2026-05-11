/// \file
/// aead_kernel_stubs.c — havocing C stubs for the kernel helpers
/// `_aead_recvmsg` calls, compiled **with kernel headers** so the
/// stubs can access kernel types by their real definitions.
///
/// scan.py drives this file through scan/compile_kernel_stubs.sh
/// (same -I flags as `crypto/algif_aead.c`), then links the result
/// alongside the kernel goto binary, the adapter, the harness, and
/// page_provenance.c.  At link time the struct definitions emitted
/// here unify with those from the kernel binary.
///
/// Fidelity (LIM-009 resolution): the two stubs whose behaviour
/// matters for the Copy Fail property now materialise concrete
/// pages:
///
///   - af_alg_get_rsgl sets up one user-writable page in
///     areq->first_rsgl.sgl.sg[0] and tags it PAGE_USER_WRITABLE.
///     This models a user iovec arriving via recvmsg().
///
///   - af_alg_pull_tsgl sets up one page in the destination SGL
///     with nondet provenance.  CBMC will pick both the safe
///     (PAGE_USER_WRITABLE) and the vulnerable (PAGE_CACHE_RO)
///     witness, so the Copy Fail scenario is reachable.
///
/// Other helpers remain pure nondet-returning stubs; see the
/// no-op section below.

#include <crypto/aead.h>
#include <crypto/if_alg.h>
#include <crypto/skcipher.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <linux/uio.h>
#include <net/sock.h>

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
// Nondet helpers (CBMC turns reads from uninitialised locals into nondet).
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

static int stub_nondet_bool(void)
{
  _Bool r;
  return r ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Shared "user" and "page-cache" page objects.
// These are the concrete struct page instances cbmc will see as the
// backing storage of user iovec pages (tagged PAGE_USER_WRITABLE) and
// TX scatterlist pages (nondet provenance).  They are static so that
// their addresses are distinct, stable objects in cbmc's memory model.
// ---------------------------------------------------------------------------

static struct page stub_user_page;
static struct page stub_tx_page;

static void init_provenance_once(void)
{
  static int initialised = 0;
  if(!initialised)
  {
    initialised = 1;
    set_page_prov(&stub_user_page, PAGE_USER_WRITABLE);
    // Leave stub_tx_page as nondet: the stub below decides its
    // provenance per-call so cbmc explores both witnesses.
  }
}

// ---------------------------------------------------------------------------
// Socket / sleep helpers.
// ---------------------------------------------------------------------------

int af_alg_wait_for_data(struct sock *sk, unsigned int flags, unsigned int min)
{
  return stub_nondet_int();
}

void lock_sock_nested(struct sock *sk, int subclass)
{
}

void release_sock(struct sock *sk)
{
}

// ---------------------------------------------------------------------------
// Allocation / free.
// ---------------------------------------------------------------------------

void *sock_kmalloc(struct sock *sk, int size, gfp_t priority)
{
  if(size <= 0)
    return NULL;
  return __CPROVER_allocate((size_t)size, 0);
}

void sock_kfree_s(struct sock *sk, void *mem, int size)
{
}

void sock_kzfree_s(struct sock *sk, void *mem, int size)
{
}

struct af_alg_async_req *
af_alg_alloc_areq(struct sock *sk, unsigned int areqlen)
{
  init_provenance_once();
  if(areqlen < sizeof(struct af_alg_async_req))
    return NULL;
  struct af_alg_async_req *areq = __CPROVER_allocate(areqlen, 0);
  if(!areq)
    return NULL;
  sg_init_table(areq->first_rsgl.sgl.sg, ALG_MAX_PAGES + 1);
  areq->first_rsgl.sgl.npages = 0;
  areq->first_rsgl.sg_num_bytes = 0;
  areq->last_rsgl = NULL;
  areq->tsgl = NULL;
  areq->tsgl_entries = 0;
  areq->outlen = 0;
  areq->areqlen = areqlen;
  return areq;
}

// ---------------------------------------------------------------------------
// Scatterlist-shaping helpers — the two that matter for the property.
// ---------------------------------------------------------------------------

/// af_alg_get_rsgl: set up one user-writable page in
/// areq->first_rsgl.  This is what the real kernel does after
/// iov_iter_get_pages'ing the user's output iovec.
int af_alg_get_rsgl(
  struct sock *sk,
  struct msghdr *msg,
  int flags,
  struct af_alg_async_req *areq,
  size_t maxsize,
  size_t *outlen)
{
  init_provenance_once();
  // One entry, pointing at stub_user_page; SG_END marker set.
  sg_set_page(&areq->first_rsgl.sgl.sg[0], &stub_user_page, 4096, 0);
  sg_mark_end(&areq->first_rsgl.sgl.sg[0]);
  areq->first_rsgl.sgl.npages = 1;
  areq->first_rsgl.sg_num_bytes = 4096;
  areq->last_rsgl = &areq->first_rsgl;
  if(outlen)
    *outlen = 4096;
  return stub_nondet_int();
}

/// af_alg_count_tsgl: bounded nondet count of TX SG entries.
unsigned int af_alg_count_tsgl(struct sock *sk, size_t bytes, size_t offset)
{
  unsigned int r = stub_nondet_uint();
  return r > 4 ? 4 : r;
}

/// af_alg_pull_tsgl: populate the destination SGL with one page whose
/// provenance is nondet on each call.  This models the worst case of
/// splice() having brought in a page-cache page the caller should not
/// be able to write to, alongside the benign case.
void af_alg_pull_tsgl(
  struct sock *sk,
  size_t used,
  struct scatterlist *dst,
  size_t dst_offset)
{
  init_provenance_once();
  if(!dst)
    return;
  // Each call assigns stub_tx_page a fresh nondet provenance, so
  // CBMC explores both USER_WRITABLE (safe) and CACHE_RO (Copy Fail)
  // witnesses.
  page_provenance_t prov;
  if(stub_nondet_bool())
    prov = PAGE_USER_WRITABLE;
  else
    prov = PAGE_CACHE_RO;
  set_page_prov(&stub_tx_page, prov);

  sg_set_page(&dst[0], &stub_tx_page, 4096, 0);
  sg_mark_end(&dst[0]);
}

// ---------------------------------------------------------------------------
// Crypto helpers (bounded nondet).
// ---------------------------------------------------------------------------

unsigned int crypto_aead_authsize(struct crypto_aead *tfm)
{
  unsigned int r = stub_nondet_uint();
  return r > 64 ? 64 : r;
}

unsigned int crypto_aead_reqsize(struct crypto_aead *tfm)
{
  unsigned int r = stub_nondet_uint();
  return r > 512 ? 512 : r;
}

unsigned int crypto_aead_ivsize(struct crypto_aead *tfm)
{
  unsigned int r = stub_nondet_uint();
  return r > 32 ? 32 : r;
}

int crypto_aead_copy_sgl(
  struct crypto_aead *null_tfm,
  struct scatterlist *src,
  struct scatterlist *dst,
  unsigned int len)
{
  return stub_nondet_int();
}

int crypto_aead_encrypt(struct aead_request *req)
{
  return stub_nondet_int();
}

int crypto_aead_decrypt(struct aead_request *req)
{
  return stub_nondet_int();
}

u32 crypto_aead_get_flags(struct crypto_aead *tfm)
{
  return stub_nondet_uint();
}

// ---------------------------------------------------------------------------
// Misc small helpers.
// ---------------------------------------------------------------------------

int aead_sufficient_data(struct sock *sk)
{
  return stub_nondet_int();
}

void crypto_init_wait(struct crypto_wait *wait)
{
}

int wait_for_completion_interruptible(struct completion *c)
{
  return stub_nondet_int();
}
