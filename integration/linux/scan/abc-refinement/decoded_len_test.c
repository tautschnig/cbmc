// Synthetic test for decoded_len_arith_overflow.ql (Gap #3).
// Stand-in macros mirror the kernel shapes (ntohl -> __builtin_bswap32,
// xdr_round_up -> round_up -> mask).  The query should flag the functions
// where a decoded length feeds round-up / multiply arithmetic.
#include <stdint.h>

#define ntohl(x) __builtin_bswap32(x)
#define round_up(x, a) (((x) + (a)-1) & ~((a)-1))
#define xdr_round_up(x) round_up((x), 4)

// (1) decoded length -> xdr_round_up used as a buffer advance.
// rxgk_verify_response CVE-2026-31633 shape: round-up wraps near UINT_MAX.
unsigned int resp_advance(const uint32_t *buf, unsigned int len)
{
  uint32_t resp_token_len = ntohl(buf[0]); // decoded from wire
  if(resp_token_len > len)
    return 0;
  // BUG: xdr_round_up(resp_token_len) can overflow to a small value,
  // so this advance/bound is wrong for resp_token_len near UINT_MAX.
  return xdr_round_up(resp_token_len);
}

// (2) decoded length -> multiply (size = count * elem).
unsigned int alloc_size(const uint32_t *buf)
{
  uint32_t nsrcs = ntohl(buf[0]); // decoded count from wire
  return nsrcs * 16;              // BUG: overflows
}

// (2b) subsystem decode accessor (ceph): a count decoded via ceph_decode_32
// fed into an allocation-size multiply (ceph osdmap/crush pattern).
extern unsigned int ceph_decode_32(void **p);
unsigned int ceph_alloc_size(void **p)
{
  unsigned int n = ceph_decode_32(p); // decoded count from the wire
  return n * 24;                      // BUG: overflows
}

// (3) NEGATIVE control: a decoded length NOT fed into round-up/multiply
// (just compared) should NOT be flagged by this oracle.
unsigned int plain_len(const uint32_t *buf, unsigned int len)
{
  uint32_t n = ntohl(buf[0]);
  if(n > len)
    return 0;
  return n;
}
