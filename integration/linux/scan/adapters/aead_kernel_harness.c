/// \file
/// aead_kernel_harness.c — entry-point harness for
/// `_aead_recvmsg(struct socket *, struct msghdr *, size_t, int)`.
///
/// Before this harness became kernel-header-aware, cbmc's symbolic
/// execution of `_aead_recvmsg` died in the very first expression
/// (`unsigned int as = crypto_aead_authsize(tfm);`) because the
/// chain of nondet pointer dereferences that expand from
/// `sock->sk` through `alg_sk(sk)->parent->private->aead->authsize`
/// left cbmc without a concrete object to target — every deref was
/// to an unresolved nondet pointer, cbmc's guard to the next stage
/// of symex folded to false, and the target call site at line 280
/// was unreachable.  The symptom was that an injected trivially
/// false contract precondition (`__CPROVER_requires(0 == 1)`) was
/// reported SUCCESS at the call site — classic vacuity.
///
/// This harness fixes the problem by explicitly constructing a
/// valid pointer graph that matches the kernel's expected shape:
///
///   struct socket        sock
///     .sk   → struct sock (embedded in struct alg_sock "child")
///   child_ask
///     .parent → parent sock (embedded in struct alg_sock "parent")
///     .private → struct af_alg_ctx
///   parent_ask
///     .private → struct aead_tfm
///       .aead   → struct crypto_aead
///       .null_tfm → struct crypto_sync_skcipher
///
/// With this graph in place, `aead_sufficient_data`'s inlined body
/// (`return ctx->used >= (ctx->enc ? 0 : as)`) and the subsequent
/// path through `af_alg_alloc_areq` are all reachable, and
/// `aead_request_set_crypt` at line 280 becomes a genuine target.
///
/// The graph is constructed with `__CPROVER_allocate` (so cbmc
/// treats each allocation as a distinct heap object) plus explicit
/// pointer wiring.  `__CPROVER_assume` forces a handful of scalar
/// fields into the range the code expects so we stay on the
/// decrypt-with-chain path that is the CVE-2026-31431 target.

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

extern int _aead_recvmsg(
  struct socket *sock,
  struct msghdr *msg,
  size_t ignored,
  int flags);

int main(void)
{
  // ----- child (this socket) -----
  struct alg_sock *child_ask = __CPROVER_allocate(sizeof(*child_ask), 0);
  struct sock *sk = &child_ask->sk; // sock is the first member.

  // ----- ctx (af_alg_ctx, reached via ask->private) -----
  struct af_alg_ctx *ctx = __CPROVER_allocate(sizeof(*ctx), 0);
  // Initialise the list head so list_for_each_entry_safe terminates.
  ctx->tsgl_list.next = &ctx->tsgl_list;
  ctx->tsgl_list.prev = &ctx->tsgl_list;
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
  return _aead_recvmsg(sock, msg, (size_t)0, flags);
}
