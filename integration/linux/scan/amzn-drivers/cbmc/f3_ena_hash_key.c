/* Slice: ena_get_rxfh() -> ena_com_get_hash_function() [device DMA-writes
 * *rss->hash_key incl. key_parts via GET_FEATURE(RSS_HASH_FUNCTION,
 * control_buffer=hash_key_dma_addr, len=sizeof(*hash_key))] ->
 * ena_com_get_hash_key(): memcpy(key, hash_key->key, key_parts*4).
 * key = rxfh->key, sized by ena_get_rxfh_key_size() = ENA_HASH_KEY_SIZE (40)
 * in ethtool's kzalloc(indir_bytes + key_size). */
#include <string.h>
#define ENA_ADMIN_RSS_KEY_PARTS 10
#define ENA_HASH_KEY_SIZE 40
typedef unsigned int u32;
typedef unsigned char u8;
struct ena_admin_feature_rss_flow_hash_control
{
  u32 key_parts;
  u32 reserved;
  u32 key[ENA_ADMIN_RSS_KEY_PARTS];
};
u32 nondet_u32(void);
static void
device_get_feature_dma(struct ena_admin_feature_rss_flow_hash_control *hk)
{
  hk->key_parts = nondet_u32(); /* whatever the device writes */
}
int ena_com_get_hash_key(
  struct ena_admin_feature_rss_flow_hash_control *hash_key,
  u8 *key)
{
  if(key)
    memcpy(
      key,
      hash_key->key,
      (unsigned long)(hash_key->key_parts) * sizeof(hash_key->key[0]));
  return 0;
}
int main(void)
{
  struct ena_admin_feature_rss_flow_hash_control hk = {
    .key_parts = ENA_ADMIN_RSS_KEY_PARTS};
  u8 key[ENA_HASH_KEY_SIZE];   /* rxfh->key */
  device_get_feature_dma(&hk); /* ena_com_get_hash_function() */
  return ena_com_get_hash_key(&hk, key);
}
