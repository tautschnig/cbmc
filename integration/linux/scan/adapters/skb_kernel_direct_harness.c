/// \file
/// skb_kernel_direct_harness.c — direct-call
/// harness for the skb_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `kfree_skb`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct sk_buff;

void skb_lifetime_init(struct sk_buff *skb,
                              unsigned int usage);
void skb_lifetime_get(struct sk_buff *skb);
void skb_lifetime_put(struct sk_buff *skb);

void kfree_skb(struct sk_buff *skb);

int main(void)
{
  static char skb_sentinel[1024];
  struct sk_buff *skb =
    (struct sk_buff *)skb_sentinel;

#ifndef FIXED
  skb_lifetime_init(skb, 1);
#else
  skb_lifetime_init(skb, 2);
#endif

  kfree_skb(skb);
  skb_lifetime_put(skb);

  kfree_skb(skb);
  skb_lifetime_put(skb);

  return 0;
}
