// SPDX-License-Identifier: GPL-2.0
/*
 * KASAN confirmation for the amzn-drivers device-trust findings.
 *
 * Linked into the same module as the REAL amzn-drivers ena_com.c, so it can
 * call ena_com_get_hash_key() directly (F3).  F2 and F6 are driven through
 * the real kernel skb APIs with the driver's own bounds.
 *
 * Each test simulates exactly one device-supplied value; nothing else is
 * changed.  Select with the `test` module parameter: 3 = F3, 2 = F2, 6 = F6.
 */
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/slab.h>

#include "ena_com.h"

static int test = 3;
module_param(test, int, 0444);
MODULE_PARM_DESC(
  test,
  "which finding to exercise: 3 (RSS key), 2 (RX frags), 6 (RX len)");

#define ENA_HASH_KEY_SIZE 40 /* ena_netdev.h */
#define ENA_PKT_MAX_BUFS 19  /* ena_netdev.h */

/*
 * F3: ena_com_get_hash_function() issues GET_FEATURE(RSS_HASH_FUNCTION) with
 * the coherent rss->hash_key buffer as the control buffer, so the device
 * writes the whole struct including key_parts.  ena_com_get_hash_key() then
 * does memcpy(key, hash_key->key, key_parts * 4) into ethtool's
 * ENA_HASH_KEY_SIZE buffer (ena_get_rxfh() calls both on every RSS query).
 */
static void test_f3_rss_key(void)
{
  struct ena_com_dev *ena_dev;
  u8 *key;

  ena_dev = kzalloc(sizeof(*ena_dev), GFP_KERNEL);
  key = kzalloc(ENA_HASH_KEY_SIZE, GFP_KERNEL); /* rxfh->key */
  if(!ena_dev || !key)
    goto out;

  /* the DMA-coherent buffer; dma_alloc_coherent is page-granular, so a
	 * long read of it does not fault -- the overflow is on the write side
	 */
  ena_dev->rss.hash_key = (void *)__get_free_page(GFP_KERNEL);
  if(!ena_dev->rss.hash_key)
    goto out;
  memset(ena_dev->rss.hash_key, 0xab, PAGE_SIZE);

  pr_info(
    "enatest: F3 key[] is %zu bytes, ENA_ADMIN_RSS_KEY_PARTS=%d, dest=%d\n",
    sizeof(ena_dev->rss.hash_key->key),
    ENA_ADMIN_RSS_KEY_PARTS,
    ENA_HASH_KEY_SIZE);

  /* --- the single device-supplied value --- */
  ena_dev->rss.hash_key->key_parts = 0x40;
  pr_info(
    "enatest: F3 device wrote key_parts=0x%x -> memcpy len %zu\n",
    ena_dev->rss.hash_key->key_parts,
    (size_t)ena_dev->rss.hash_key->key_parts * 4);

  ena_com_get_hash_key(ena_dev, key); /* REAL driver function */
  pr_info("enatest: F3 returned (key[0]=%u)\n", key[0]);

  free_page((unsigned long)ena_dev->rss.hash_key);
out:
  kfree(key);
  kfree(ena_dev);
}

/*
 * F2: the only bound on the per-packet RX descriptor count is
 * nb_hw_desc <= max_rx_sgl_size = min(ENA_PKT_MAX_BUFS=19, device value)
 * (ena_eth_com.c:603, ena_netdev.c:4111).  Descriptor 0 becomes the linear
 * part and 1..18 become frags, so with CONFIG_MAX_SKB_FRAGS=17 the 18th
 * frag write lands past frags[].  ena_fill_rx_frags() does exactly this
 * loop with skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, ...).
 */
static void test_f2_rx_frags(void)
{
  struct sk_buff *skb;
  struct page *page;
  u16 descs = ENA_PKT_MAX_BUFS; /* device sent the maximum allowed */
  u16 buf;

  pr_info(
    "enatest: F2 MAX_SKB_FRAGS=%d, driver allows descs<=%d -> %d frags\n",
    MAX_SKB_FRAGS,
    ENA_PKT_MAX_BUFS,
    ENA_PKT_MAX_BUFS - 1);

  skb = alloc_skb(256, GFP_KERNEL);
  if(!skb)
    return;

  for(buf = 1; buf < descs; ++buf)
  { /* ena_fill_rx_frags() */
    page = alloc_page(GFP_KERNEL);
    if(!page)
      break;
    pr_info(
      "enatest: F2 adding frag %u (nr_frags=%u)\n",
      buf,
      skb_shinfo(skb)->nr_frags);
    skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page, 0, 64, PAGE_SIZE);
  }
  pr_info("enatest: F2 done, nr_frags=%u\n", skb_shinfo(skb)->nr_frags);
  kfree_skb(skb);
}

/*
 * F6: cdesc->base.length (u16, device) is never compared with the posted
 * buffer.  ena_rx_skb() builds the skb over one page and then does
 * skb_put(skb, len), so an over-large len runs past the buffer
 * (skb_over_panic / KASAN on the page).
 */
static void test_f6_rx_len(void)
{
  struct sk_buff *skb;
  void *buf;
  u16 len = 36859; /* the CBMC witness value */
  int buf_len = PAGE_SIZE;

  buf = (void *)__get_free_page(GFP_KERNEL);
  if(!buf)
    return;

  skb = build_skb(buf, buf_len); /* ena_alloc_skb() */
  if(!skb)
  {
    free_page((unsigned long)buf);
    return;
  }
  skb_reserve(skb, 256); /* rx_info->buf_offset */
  pr_info(
    "enatest: F6 buf_len=%d tailroom=%u, device len=%u\n",
    buf_len,
    skb_tailroom(skb),
    len);
  skb_put(skb, len); /* ena_rx_skb() */
  pr_info("enatest: F6 skb_put survived, skb->len=%u\n", skb->len);
  kfree_skb(skb);
}

/*
 * F1: EFA efa_request_doorbell_bar().  db_bar_idx is a u8 copied from the
 * device's GET_FEATURE(DEVICE_ATTR) reply.  `int db_bar = BIT(db_bar_idx)`
 * truncates to 0 for indices 32..63, so both BAR-mask checks pass, and
 * pci_resource_start(pdev, db_bar_idx) then indexes
 * pdev->resource[PCI_NUM_RESOURCES].  Real struct pci_dev, real macro.
 */
static void test_f1_db_bar(void)
{
  struct pci_dev *pdev;
  u8 db_bar_idx = 32; /* the CBMC witness value */
  int db_bar;
  u64 addr;

  pdev = kzalloc(sizeof(*pdev), GFP_KERNEL);
  if(!pdev)
    return;

  db_bar = BIT(db_bar_idx); /* truncates to 0 in an int */
  pr_info(
    "enatest: F1 device db_bar=%u -> BIT() in int = %d (checks pass: %d)\n",
    db_bar_idx,
    db_bar,
    db_bar == 0);
  pr_info(
    "enatest: F1 PCI_NUM_RESOURCES=%d, reading resource[%u]\n",
    PCI_NUM_RESOURCES,
    db_bar_idx);

  addr = pci_resource_start(pdev, db_bar_idx); /* OOB read */
  pr_info("enatest: F1 pci_resource_start returned %llx\n", addr);
  kfree(pdev);
}

/*
 * F4: EFA kverbs efa_poll_sub_cq().  wq->wrid / wrid_idx_pool are
 * kvzalloc(max_wqes) (efa_verbs.c:1087-1091); wrid_idx = cqe->req_id is
 * device-written and used unchecked:
 *   wq->wrid_idx_pool_next--;  wrid_idx_pool[next] = wrid_idx;
 *   wc->wr_id = wq->wrid[wrid_idx];
 */
static void test_f4_wrid(void)
{
  u32 max_wqes = 64;
  u64 *wrid;
  u32 *wrid_idx_pool, wrid_idx_pool_next = 1;
  u32 wrid_idx = 4096; /* cqe->req_id from the device */
  u64 wr_id;

  wrid = kvzalloc(max_wqes * sizeof(*wrid), GFP_KERNEL);
  wrid_idx_pool = kvzalloc(max_wqes * sizeof(*wrid_idx_pool), GFP_KERNEL);
  if(!wrid || !wrid_idx_pool)
    goto out;

  pr_info("enatest: F4 max_wqes=%u, device req_id=%u\n", max_wqes, wrid_idx);
  wrid_idx_pool_next--;
  wrid_idx_pool[wrid_idx_pool_next] = wrid_idx;
  wr_id = wrid[wrid_idx]; /* OOB read */
  pr_info("enatest: F4 wr_id=%llx\n", wr_id);
out:
  kvfree(wrid_idx_pool);
  kvfree(wrid);
}

/*
 * F5: EFA kverbs efa_create_qp_kernel() passes create_qp_resp.send_sub_cq_idx
 * (device reply) to efa_cq_inc_ref_cnt(), which does
 * cq->sub_cq_arr[idx].ref_cnt++ on an array of sub_cqs_per_cq entries.
 */
struct t_sub_cq
{
  u8 *buf;
  u32 cqe_size, queue_mask, ref_cnt, consumed_cnt;
  int phase;
};

static void test_f5_sub_cq(void)
{
  u16 sub_cqs_per_cq = 1;
  u16 send_sub_cq_idx = 137; /* the CBMC witness value */
  struct t_sub_cq *sub_cq_arr;

  sub_cq_arr = kvzalloc(sub_cqs_per_cq * sizeof(*sub_cq_arr), GFP_KERNEL);
  if(!sub_cq_arr)
    return;
  pr_info(
    "enatest: F5 sub_cqs_per_cq=%u, device send_sub_cq_idx=%u\n",
    sub_cqs_per_cq,
    send_sub_cq_idx);
  sub_cq_arr[send_sub_cq_idx].ref_cnt++; /* OOB increment */
  pr_info("enatest: F5 survived\n");
  kvfree(sub_cq_arr);
}

static int __init enatest_init(void)
{
  pr_info("enatest: running test %d\n", test);
  switch(test)
  {
  case 3:
    test_f3_rss_key();
    break;
  case 2:
    test_f2_rx_frags();
    break;
  case 6:
    test_f6_rx_len();
    break;
  case 1:
    test_f1_db_bar();
    break;
  case 4:
    test_f4_wrid();
    break;
  case 5:
    test_f5_sub_cq();
    break;
  default:
    pr_info("enatest: unknown test\n");
    break;
  }
  pr_info("enatest: test %d complete\n", test);
  return 0;
}

static void __exit enatest_exit(void)
{
}

module_init(enatest_init);
module_exit(enatest_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KASAN harness for amzn-drivers device-trust findings");
