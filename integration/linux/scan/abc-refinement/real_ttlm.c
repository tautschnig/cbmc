// Real-function reach harness for the bounded-cursor candidate
// ieee80211_get_ttlm (net/mac80211/mlme.c), UNGUARDED in the
// caller-precondition analysis (a genuine concern, not a caller-resolved
// FP).  Tiny and fully verbatim: the read width (1 or 2 bytes) is chosen
// by the attacker-influenced bm_size, with no length argument to bound it.
#include "cover_probe.h"
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;

static u16 get_unaligned_le16(const void *p)
{
  const u8 *b = p;
  return b[0] | (u16)b[1] << 8;
}

// ---- VERBATIM (net/mac80211/mlme.c) ----
static u16 ieee80211_get_ttlm(u8 bm_size, u8 *data)
{
  if(bm_size == 1)
    return *data;

  return get_unaligned_le16(data); // reads data[0..1]
}

u8 nd(void)
{
  u8 x;
  return x;
}

// vulnerable: caller hands `map_size` bytes but ieee80211_get_ttlm reads a
// width chosen by bm_size; if bm_size != 1 it reads 2 bytes even when only
// 1 is available.
int probe_vuln(void)
{
  u8 bm_size = nd();
  unsigned avail = nd();
  __CPROVER_assume(avail >= 1 && avail <= 2);
  u8 *data = malloc(avail);
  __CPROVER_assume(data != 0);
  CHECKPOINT(oob, bm_size != 1 && avail < 2); // 2-byte read, <2 available
  return ieee80211_get_ttlm(bm_size, data);
}

// fixed: the caller guarantees the read width's worth of bytes
// (map_size == 1 for bm_size==1, else 2).
int probe_fixed(void)
{
  u8 bm_size = nd();
  unsigned avail = nd();
  __CPROVER_assume(avail >= 1 && avail <= 2);
  __CPROVER_assume(avail >= (bm_size == 1 ? 1u : 2u));
  u8 *data = malloc(avail);
  __CPROVER_assume(data != 0);
  CHECKPOINT(oob, bm_size != 1 && avail < 2);
  return ieee80211_get_ttlm(bm_size, data);
}
