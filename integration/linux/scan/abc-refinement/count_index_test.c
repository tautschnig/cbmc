// Synthetic test for tainted_count_into_fixed_array.ql.
// Mirrors the real CVE shapes; the query should flag the BUGGY cases and
// NOT the GUARDED ones.
#include <string.h>

#define DPSW_MAX_IF 64
#define NUM_KEYS 4

struct attr
{
  int num_ifs;
};
struct cfg
{
  int if_id[DPSW_MAX_IF];
  int num_ifs;
};
struct sw
{
  struct attr sw_attr;
  int *ports_idx;
};

// (A) BUGGY: count-loop-write — dpaa2 CVE-2026-43205 shape.
// num_ifs (from firmware) drives a loop writing into fixed if_id[64],
// no bound check.
void flood_cfg_buggy(struct sw *ethsw, struct cfg *cfg)
{
  int i = 0, j;
  for(j = 0; j < ethsw->sw_attr.num_ifs; j++)
    cfg->if_id[i++] = ethsw->ports_idx[j];
  cfg->num_ifs = i;
}

// (A') GUARDED negative control: same but with a bound check.
void flood_cfg_fixed(struct sw *ethsw, struct cfg *cfg)
{
  int i = 0, j;
  if(ethsw->sw_attr.num_ifs > DPSW_MAX_IF)
    return;
  for(j = 0; j < ethsw->sw_attr.num_ifs; j++)
    cfg->if_id[i++] = ethsw->ports_idx[j];
  cfg->num_ifs = i;
}

// (B) BUGGY: direct-index — b43/wl1251 CVE-2026-46122 shape.
// A firmware-supplied key index used directly to subscript a fixed array.
int tx_frames[NUM_KEYS];
int rx_key_buggy(int key_index)
{
  return tx_frames[key_index]; // no bound check on key_index
}

// (B') GUARDED negative control.
int rx_key_fixed(int key_index)
{
  if(key_index >= NUM_KEYS)
    return -1;
  return tx_frames[key_index];
}
