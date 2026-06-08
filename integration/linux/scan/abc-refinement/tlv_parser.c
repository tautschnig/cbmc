// Real-code-style goto-cc harness prototype: a TLV/IE parse loop.
//
// Models the classic 802.11-style "walk a buffer of {type,len,value}
// elements, advancing by the attacker-supplied len" pattern (cf.
// rtw_set_ie / IE walkers).  The single-memcpy scaled model cannot
// express this; CBMC's LOOP UNWINDING does -- an unchecked advance
// past the buffer end is a reachable OOB read with a concrete trace.
//
// Build (demonstrates the goto-binary workflow that scales to real .o):
//   goto-cc -o tlv.gb tlv_parser.c
//   cbmc tlv.gb --function harness_parse_buggy  --bounds-check --unwind 12
//   cbmc tlv.gb --function harness_parse_fixed  --bounds-check --unwind 12
//
//   buggy -> VERIFICATION FAILED (array bounds in body[i] read)
//   fixed -> VERIFICATION SUCCESSFUL
#include <stdint.h>

#define BUFLEN 8 // scaled fixed input buffer (bounded model)

unsigned char nd_uchar(void)
{
  unsigned char x;
  return x;
}

// BUGGY: trusts each element's length; advances pos by 2+len without
// checking that the value bytes fit in [pos+2, total).  An element
// near the end with a large len walks the read off the end.
int harness_parse_buggy(void)
{
  unsigned char body[BUFLEN];
  unsigned int total = nd_uchar();
  __CPROVER_assume(total <= BUFLEN);

  unsigned int pos = 0;
  unsigned int sum = 0;
  while(pos + 2 <= total) // header (type,len) fits
  {
    unsigned char len = body[pos + 1]; // attacker-controlled length
    unsigned int i = 0;
    while(i < len)
    {
      sum += body[pos + 2 + i]; // <-- OOB read when pos+2+i >= BUFLEN
      i++;
    }
    pos += 2 + len; // advance by element size (unchecked)
  }
  return sum;
}

// FIXED: only consume an element whose value fully fits in the buffer.
int harness_parse_fixed(void)
{
  unsigned char body[BUFLEN];
  unsigned int total = nd_uchar();
  __CPROVER_assume(total <= BUFLEN);

  unsigned int pos = 0;
  unsigned int sum = 0;
  while(pos + 2 <= total)
  {
    unsigned char len = body[pos + 1];
    if(pos + 2 + len > total) // bound the element against the buffer
      break;
    unsigned int i = 0;
    while(i < len)
    {
      sum += body[pos + 2 + i]; // in-bounds: pos+2+len <= total <= BUFLEN
      i++;
    }
    pos += 2 + len;
  }
  return sum;
}
