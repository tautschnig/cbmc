// A1-UB real-candidate discharge: mldv2_mrd (include/net/mld.h), flagged by
// tainted_ub_arith as an undefined-shift candidate (shift amount derives
// from a wire field mld2q_mrc).  Verbatim body + real macros.  Obligation:
// --undefined-shift-check.  Expected: SAFE -- the exponent is masked to 3
// bits (0..7) so the shift is 3..10 on a 64-bit value.  A stage-2 clearing
// (the bitmask is the mitigation the finder's taint gate does not model).
#include <stdint.h>

typedef uint16_t __be16;

// real macros
#define MLDV2_MRC_EXP(value) (((value) >> 12) & 0x0007)
#define MLDV2_MRC_MAN(value) ((value) & 0x0fff)
#define MLD_MRC_MIN_THRESHOLD 32768UL

static uint16_t ntohs_model(__be16 x)
{
  return (uint16_t)((x >> 8) | (x << 8));
}

uint16_t nd(void)
{
  uint16_t x;
  return x;
}

// VERBATIM mldv2_mrd, mlh2->mld2q_mrc modelled as the attacker wire field.
unsigned long mldv2_mrd_probe(void)
{
  __be16 mld2q_mrc = nd();
  unsigned long mc_mrc = ntohs_model(mld2q_mrc);

  if(mc_mrc < MLD_MRC_MIN_THRESHOLD)
  {
    return mc_mrc;
  }
  else
  {
    unsigned long mc_man, mc_exp;
    mc_exp = MLDV2_MRC_EXP(mc_mrc);
    mc_man = MLDV2_MRC_MAN(mc_mrc);
    return (mc_man | 0x1000) << (mc_exp + 3); // shift 3..10 -> defined
  }
}
