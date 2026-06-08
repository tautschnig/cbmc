// Synthetic test for skb_field_before_lencheck.ql.
// The query should flag the BUGGY functions (read skb->data with no length
// guard) and NOT the GUARDED ones.
#include <stdint.h>
#include <string.h>

struct sk_buff
{
  unsigned int len;
  uint8_t *data;
};

struct hdr
{
  uint16_t sdu_len;
  uint16_t scid;
};

extern int pskb_may_pull(struct sk_buff *skb, unsigned int len);
extern uint16_t get_unaligned_le16(const void *p);

// (1) BUGGY: cast skb->data to struct and read field, no length guard.
// L2CAP ecred CVE-2026-31513 shape.
int ecred_conn_req_buggy(struct sk_buff *skb)
{
  struct hdr *h = (struct hdr *)skb->data;
  return h->scid; // reads 2 bytes at offset 2 with no skb->len check
}

// (1') GUARDED: pull-check before the cast.
int ecred_conn_req_fixed(struct sk_buff *skb)
{
  struct hdr *h;
  if(!pskb_may_pull(skb, sizeof(struct hdr)))
    return -1;
  h = (struct hdr *)skb->data;
  return h->scid;
}

// (2) BUGGY: get_unaligned_le16(skb->data) with no guard.
// L2CAP ecred_data_rcv CVE-2026-31512 shape.
int data_rcv_buggy(struct sk_buff *skb)
{
  uint16_t sdu_len = get_unaligned_le16(skb->data);
  return sdu_len;
}

// (2') GUARDED: pull-check first.
int data_rcv_fixed(struct sk_buff *skb)
{
  uint16_t sdu_len;
  if(!pskb_may_pull(skb, 2))
    return -1;
  sdu_len = get_unaligned_le16(skb->data);
  return sdu_len;
}

// (3) GUARDED via explicit len compare: should be excluded.
int data_rcv_lencheck(struct sk_buff *skb)
{
  if(skb->len < 2)
    return -1;
  return get_unaligned_le16(skb->data);
}
