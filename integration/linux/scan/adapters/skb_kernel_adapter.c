/// \file
/// skb_kernel_adapter.c — attaches the
/// skb_lifetime property module's `skb_live`
/// predicate as a contract precondition on the kernel's
/// `kfree_skb` API.

struct sk_buff;

int skb_live(struct sk_buff *skb);

// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
void kfree_skb(struct sk_buff *skb)
  __CPROVER_requires(skb != (struct sk_buff *)0)
  __CPROVER_requires(skb_live(skb) == 1)
  __CPROVER_assigns();
