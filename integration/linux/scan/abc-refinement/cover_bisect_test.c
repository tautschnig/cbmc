// Synthetic fixture for cover-probe "assumption bisection".
//
// Models the situation the talk describes: a model (or an oracle) claims a
// path A->B->C->D to a vulnerable sink, asserting an assumption at each
// hop.  We sprinkle CHECKPOINT() probes carrying those assumptions and let
// CBMC tell us which hops are actually feasible -- pinpointing the first
// hop where the claim breaks.
//
// Claimed path to the copy at D:
//   A: msg type is 2 (the parsed-record dispatch)
//   B: we are past the type gate
//   C: the record length is > 100  (the claim: "a long record reaches the
//      copy and overflows dst[64]")
//   D: the copy site
// Reality: an earlier guard rejects len > 64, so the assumption at C
// (len > 100 at the copy) is INFEASIBLE.  The bisection must report
// checkpoint C as the first blocked hop -- the claimed overflow path does
// not exist as described.
#include "cover_probe.h"

unsigned nd(void);

int parse_claimed_overflow(void)
{
  unsigned type = nd();
  unsigned len = nd();
  char dst[64];

  CHECKPOINT(1, type == 2); // A: reachable with type==2
  if (type != 2)
    return -1;
  CHECKPOINT(2, 1); // B: past the type gate (reachable)
  if (len > 64)     // guard clamps the length
    return -1;
  CHECKPOINT(3, len > 100); // C: CLAIM len>100 here -- INFEASIBLE (len<=64)
  CHECKPOINT(4, 1);         // D: the copy site (location reachable)
  for (unsigned i = 0; i < len; i++)
    dst[i] = (char)i;
  return dst[0];
}

// Control: a path whose every claimed hop IS feasible (no clamp), so all
// checkpoints are SATISFIED and the bisection reports "no break".
int parse_real_overflow(void)
{
  unsigned type = nd();
  unsigned len = nd();
  char dst[64];

  CHECKPOINT(1, type == 2);
  if (type != 2)
    return -1;
  CHECKPOINT(2, 1);
  CHECKPOINT(3, len > 100); // feasible: no clamp on len
  CHECKPOINT(4, 1);
  for (unsigned i = 0; i < len; i++)
    dst[i] = (char)i; // genuinely reachable OOB
  return dst[0];
}
