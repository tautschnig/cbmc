// CBMC harness for Gap #3: integer overflow in length round-up arithmetic
// feeding a buffer bound/advance (the rxgk CVE-2026-31633 shape).
//
// Build: goto-cc -o x.gb decoded_len_overflow.c
//   cbmc x.gb --function harness_roundup_buggy --bounds-check \
//        --unsigned-overflow-check --unwind 4
//   cbmc x.gb --function harness_roundup_fixed --bounds-check \
//        --unsigned-overflow-check --unwind 4
#include <stdint.h>

// kernel round_up to multiple of 4: (((x)+3) & ~3u) -- wraps near UINT_MAX
#define xdr_round_up(x) (((x) + 3u) & ~3u)

uint32_t nd_u32(void)
{
  uint32_t x;
  return x;
}

// (buggy) A length decoded from the wire is rounded up to a multiple of 4
// *before* being bounded.  `token_len + 3` overflows for token_len near
// UINT_MAX, so the rounded value wraps to a tiny number -- the exact
// rxgk CVE-2026-31633 primitive.  CBMC's unsigned-overflow-check decides
// the overflow is reachable; a pattern-matcher cannot.
uint32_t harness_roundup_buggy(void)
{
  uint32_t token_len = nd_u32(); // attacker-controlled, from the wire
  // BUG: round up an unbounded length -> token_len + 3 can overflow.
  uint32_t rounded = xdr_round_up(token_len);
  return rounded;
}

// (fixed) Reject lengths large enough to overflow the round-up first.
uint32_t harness_roundup_fixed(void)
{
  uint32_t token_len = nd_u32();
  if(token_len > 0xfffffffbu) // UINT_MAX - 3: round-up cannot overflow
    return 0;
  uint32_t rounded = xdr_round_up(token_len);
  return rounded;
}

// (buggy2) size = count * elem, count decoded from the wire, unbounded.
uint32_t harness_multiply_buggy(void)
{
  uint32_t nsrcs = nd_u32();
  return nsrcs * 16u; // BUG: multiplication overflows
}

// (fixed2) bound the count so the product cannot overflow.
uint32_t harness_multiply_fixed(void)
{
  uint32_t nsrcs = nd_u32();
  if(nsrcs > 0xffffffffu / 16u)
    return 0;
  return nsrcs * 16u;
}
