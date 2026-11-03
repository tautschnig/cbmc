/* Slice: ENA RX multi-buffer path (amzn-drivers ena_netdev.c 4111 / 358 / 1630,
 * ena_eth_com.c 603-616, ena_fill_rx_frags 1298-1345 -> skb_add_rx_frag).
 * Same shape in mainline ena_netdev.c 2922 / ena_rx_skb do-while. */
#define ENA_PKT_MAX_BUFS 19
#define MAX_SKB_FRAGS                                                          \
  17 /* CONFIG_MAX_SKB_FRAGS default (Kconfig range 17..45) */
typedef unsigned short u16;
struct skb_frag
{
  void *p;
  unsigned off, len;
};
struct skb_shared_info
{
  unsigned char nr_frags;
  struct skb_frag frags[MAX_SKB_FRAGS];
};
u16 nondet_u16(void);
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
static void skb_add_rx_frag(struct skb_shared_info *sh, int i)
{
  sh->frags[i].len = 1;
  sh->nr_frags = i + 1;
}
int main(void)
{
  struct skb_shared_info sh = {0};
  u16 max_packet_rx_descs = nondet_u16(); /* device feature reply */
  u16 sgl_size = min_t(u16, ENA_PKT_MAX_BUFS, max_packet_rx_descs);
  u16 nb_hw_desc = nondet_u16(); /* device completion descs */
  if(nb_hw_desc == 0)
    return 0;
  if(nb_hw_desc > sgl_size)
    return -28;                             /* -ENOSPC: the only bound */
  for(u16 buf = 1; buf < nb_hw_desc; ++buf) /* ena_fill_rx_frags */
    skb_add_rx_frag(&sh, sh.nr_frags);
  return 0;
}
