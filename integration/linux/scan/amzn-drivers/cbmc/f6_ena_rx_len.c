/* Slice: ENA RX linear path (amzn-drivers ena_netdev.c ena_rx_skb 1442-1475;
 * mainline ena_rx_skb same shape).  len = cdesc->base.length (u16, device).
 * The posted buffer is one page minus headroom/tailroom; build_skb gets
 * frag_size = buf_len derived FROM len, then skb_put(skb, len) is checked
 * against tailroom by skb_over_panic, so the observable effect is a BUG()
 * when len exceeds what the page can hold (DoS).  Modelled as an assertion
 * on the device-supplied length staying inside the page. */
#include <assert.h>
typedef unsigned short u16;
#define ENA_PAGE_SIZE 4096
#define SKB_DATA_ALIGN(x) (((x) + 63) & ~63)
#define TAILROOM 320 /* SKB_DATA_ALIGN(sizeof(skb_shared_info)) */
u16 nondet_u16(void);
int main(void)
{
  int rx_headroom = 256, page_offset = 0;
  int buf_offset = rx_headroom; /* rx_info->buf_offset */
  u16 len = nondet_u16();       /* ena_bufs[0].len <- cdesc->base.length */
  u16 rx_copybreak = 256;
  if(len <= rx_copybreak)
    return 0;                                /* copybreak path */
  int buf_len = ENA_PAGE_SIZE - page_offset; /* no reuse */
  /* skb data area = buf_len - TAILROOM; skb_reserve(buf_offset); skb_put(len) */
  assert(buf_offset + len <= buf_len - TAILROOM); /* else skb_over_panic */
  return 0;
}
