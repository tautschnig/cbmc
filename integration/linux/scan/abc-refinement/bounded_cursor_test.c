// Synthetic bounded-cursor parser fixture for the taint layer / generalised
// skb_field_before_lencheck oracle.  Mirrors the ceph (void *p, void *end)
// parser shape behind CVE-2026-43406/43407: read fields from a cursor and
// advance it.  buggy = no bounds check before the reads; fixed = guarded
// with a ceph_decode_need-style check.
#include <stdint.h>

extern uint32_t ceph_decode_32(void **p);
// returns nonzero if at least n bytes remain (a bounds-check sanitizer)
extern int ceph_decode_need(void **p, void *end, int n);

// (1) BUGGY: reads from the (buf, len) cursor with NO length guard.
int parse_buggy(void *buf, unsigned long len)
{
  void *p = buf;
  void *end = buf + len;
  uint32_t a = ceph_decode_32(&p); // OOB read: no check p+4 <= end
  uint32_t n = ceph_decode_32(&p);
  (void)end;
  return (int)(a + n);
}

// (1') FIXED: guarded with ceph_decode_need before the reads.
int parse_fixed(void *buf, unsigned long len)
{
  void *p = buf;
  void *end = buf + len;
  if(!ceph_decode_need(&p, end, 8))
    return -1;
  uint32_t a = ceph_decode_32(&p);
  uint32_t n = ceph_decode_32(&p);
  return (int)(a + n);
}

// (2) NEGATIVE control: not a bounded-buffer parser (no cursor/len pair);
// reads a decode accessor from a struct field, not an attacker cursor.
struct hdr
{
  uint32_t x;
};
int not_a_parser(struct hdr *h)
{
  return (int)ceph_decode_32((void **)&h->x);
}
