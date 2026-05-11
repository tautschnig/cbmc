/// \file
/// aead_kernel_harness.c — entry-point harness for
/// `_aead_recvmsg` in `crypto/algif_aead.c`.
///
/// Preconditions that had to be met to get cbmc to drive through
/// `_aead_recvmsg` and reach its call to `aead_request_set_crypt`
/// (line 280) — discovered iteratively while resolving LIM-009:
///
/// 1. `_aead_recvmsg` is `static` in `crypto/algif_aead.c`, so a
///    plain C `extern` declaration does not bind to the kernel
///    TU's body.  `goto-cc --export-file-local-symbols` exposes
///    the mangled name
///    `__CPROVER_file_local_algif_aead_c__aead_recvmsg`, which is
///    the symbol we must call below.  Without the mangled-name
///    binding, the harness silently invoked an empty external stub
///    and the scan was vacuously SUCCESSFUL — this was the actual
///    root cause of LIM-009's "call site unreachable" symptom.
///
/// 2. cbmc needs a concrete pointer graph, not nondet pointers, so
///    that `aead_sufficient_data` and its siblings (all
///    static-inline with nested derefs) can compute their path
///    conditions without CBMC guard-folding to false.  We allocate
///    `struct alg_sock` for both the child and parent sockets, a
///    `struct af_alg_ctx`, a `struct aead_tfm`, and the
///    `struct crypto_aead` / `struct crypto_sync_skcipher` the
///    parent's `->private` points at, then wire them together.
///
/// 3. `ctx->tsgl_list` must be non-empty with one `af_alg_tsgl`
///    entry holding a valid `sg[0]` (nonzero length, non-NULL
///    page).  Otherwise `_aead_recvmsg` takes the
///    `if (processed && !tsgl_src) goto free` early-return and
///    the target call site is unreached.
///
/// 4. `ctx->{init,more,enc,used,aead_assoclen}` and `tfm->authsize`
///    must be set such that `aead_sufficient_data` returns true and
///    `outlen = used - authsize` does not underflow.
///
/// 5. `sgl_all_user_writable` — the adapter's pure predicate that
///    the contract's `__CPROVER_requires` refers to — has to stay
///    reachable in the CFG so `goto-instrument --aggressive-slice`
///    (which scan.py applies to keep the SAT formula tractable)
///    does not drop its body.  We force-anchor it with a harmless
///    reachable call below plus an explicit
///    `--aggressive-slice-preserve-function` in scan.py.
///
/// None of this would matter without symbol issues #1 and #5:
/// those are the two latches that hid the real behaviour.

#include <crypto/aead.h>
#include <crypto/if_alg.h>
#include <crypto/skcipher.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/uio.h>
#include <net/sock.h>

// The aead_tfm struct is a local definition inside algif_aead.c; we
// reproduce its shape here so the harness can allocate one and wire
// pointers through it.
struct aead_tfm
{
  struct crypto_aead *aead;
  struct crypto_sync_skcipher *null_tfm;
};

// Name-mangled forms of kernel `static` symbols exposed by
// goto-cc --export-file-local-symbols; see point 1 above.
extern int __CPROVER_file_local_algif_aead_c__aead_recvmsg(
  struct socket *sock,
  struct msghdr *msg,
  size_t ignored,
  int flags);

// File-scope static backing page so cbmc sees a stable object with
// a distinct address.  Used below as the sg_page target in the
// ctx->tsgl_list entry.
static struct page harness_dummy_tx_page;

// Adapter-provided predicate — declared here so the harness can
// force-keep it alive through `--aggressive-slice` (see point 5).
int sgl_all_user_writable(struct scatterlist *sgl);

// Volatile sink so the compiler/cbmc cannot fold the predicate call
// away at the anchor site in main().
volatile int harness_slicer_anchor;

int main(void)
{
  // ----- child (this socket) -----
  struct alg_sock *child_ask = __CPROVER_allocate(sizeof(*child_ask), 0);
  struct sock *sk = &child_ask->sk; // sock is the first member.

  // ----- ctx (af_alg_ctx, reached via ask->private) -----
  struct af_alg_ctx *ctx = __CPROVER_allocate(sizeof(*ctx), 0);

  // A single TX scatterlist page must be present on ctx->tsgl_list,
  // or `_aead_recvmsg` takes the `if (processed && !tsgl_src)`
  // early-return and the target call site at line 280 is unreached.
  //
  // `struct af_alg_tsgl` ends with a flexible `sg[]` array; to
  // avoid tripping cbmc symex on flexible-array writes through a
  // heap pointer, we allocate the list-head wrapper and the
  // scatterlist array as *separate* objects and wire them through
  // the `list_head` embedded in the wrapper.  We rely on the fact
  // that in the kernel, `struct af_alg_tsgl`'s `sg` is the last
  // field, so the wrapper's `&tsgl_entry->list` is the member the
  // `list_for_each_entry_safe` macro walks.
  struct af_alg_tsgl *tsgl_entry = __CPROVER_allocate(
    sizeof(struct af_alg_tsgl) + sizeof(struct scatterlist), 0);
  tsgl_entry->cur = 1;
  // Build the sg entry in place via pointer arithmetic on the
  // flexible `sg[]` tail.  Writing through `&tsgl_entry->sg[0]`
  // directly can hit cbmc's symex UNREACHABLE when the lhs is a
  // symbol expression whose type inference sees a zero-length
  // array; going through a properly-typed pointer sidesteps that.
  struct scatterlist *sg0 = (struct scatterlist *)&tsgl_entry->sg[0];
  sg0->page_link = (unsigned long)&harness_dummy_tx_page | 2u /* SG_END */;
  sg0->offset = 0;
  sg0->length = 4096;

  // Link the wrapper into ctx->tsgl_list.
  ctx->tsgl_list.next = &tsgl_entry->list;
  ctx->tsgl_list.prev = &tsgl_entry->list;
  tsgl_entry->list.next = &ctx->tsgl_list;
  tsgl_entry->list.prev = &ctx->tsgl_list;

  // Drive the path into the vulnerable decrypt branch, with
  // non-empty processed/used such that the aead_request_set_crypt
  // site at line 280 is reached.
  ctx->init = 1;
  ctx->more = 0;
  ctx->enc = 0; // decrypt
  ctx->aead_assoclen = 0;
  ctx->used = 64;
  child_ask->private = ctx;

  // ----- parent socket -----
  struct alg_sock *parent_ask = __CPROVER_allocate(sizeof(*parent_ask), 0);
  struct sock *psk = &parent_ask->sk;
  child_ask->parent = psk;

  // ----- aeadc (aead_tfm, reached via pask->private) -----
  struct aead_tfm *aeadc = __CPROVER_allocate(sizeof(*aeadc), 0);
  struct crypto_aead *tfm = __CPROVER_allocate(sizeof(*tfm), 0);
  struct crypto_sync_skcipher *null_tfm =
    __CPROVER_allocate(sizeof(*null_tfm), 0);
  aeadc->aead = tfm;
  aeadc->null_tfm = null_tfm;
  parent_ask->private = aeadc;

  // authsize must be less-than-or-equal to used so that
  // outlen = used - authsize does not underflow.
  tfm->authsize = 16;

  // ----- socket + msghdr -----
  struct socket *sock = __CPROVER_allocate(sizeof(*sock), 0);
  sock->sk = sk;
  struct msghdr *msg = __CPROVER_allocate(sizeof(*msg), 0);
  // msg_iocb is checked in the AIO branch; setting it NULL forces
  // the sync path which is the CVE-2026-31431 code path.
  msg->msg_iocb = NULL;

  int flags = 0;

  // Anchor `sgl_all_user_writable` (the adapter's pure predicate) in
  // the CFG so `goto-instrument --aggressive-slice` does not drop
  // its body.  The contract attached to
  // `__CPROVER_file_local_aead_h_aead_request_set_crypt` references
  // this predicate in its `__CPROVER_requires` clause, but that call
  // site is not visible to the slicer as a normal CFG edge.  An
  // explicit reachable call forces the body to be preserved.
  struct scatterlist anchor_sg;
  anchor_sg.page_link = 0;
  harness_slicer_anchor = sgl_all_user_writable(&anchor_sg);

  return __CPROVER_file_local_algif_aead_c__aead_recvmsg(
    sock, msg, (size_t)0, flags);
}
