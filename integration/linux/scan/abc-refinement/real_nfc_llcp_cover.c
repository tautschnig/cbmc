// Cover-probe "assumption bisection" on the REAL CVE-2026-31622 function
// (nfc_llcp_parse_gb_tlv), reusing the verbatim parser logic from
// real_nfc_llcp_parse.c.  Instead of asking "is the bug shape reachable"
// (the closed-loop bounds-check question), we ask the defender's
// reachability question directly with a cover probe:
//
//   "Can the parser reach the point of reading an element's VALUE bytes
//    while the element claims more bytes than remain in the buffer?"
//   i.e.  offset + 2 + length > tlv_array_len  at the use site.
//
// That is exactly the OOB-read precondition behind the CVE.  The probe is
// placed at the value-read site in both the vulnerable and the fixed
// parser; cover_probe.py reports it SATISFIED (precondition reachable) for
// the vulnerable body and BLOCKED (ruled out by the fix's length guard)
// for the fixed body.
#include "cover_probe.h"
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;

#define LLCP_TLV_MIUX 0x2

struct nfc_llcp_local
{
  u16 remote_miu;
};

// vulnerable: no per-element bound; reaches the value-read with an element
// that overruns the buffer.
int llcp_parse_vuln(struct nfc_llcp_local *local, const u8 *tlv_array,
                    u16 tlv_array_len)
{
  const u8 *tlv = tlv_array;
  u8 type, length, offset = 0;

  if(local == NULL)
    return -1;

  while(offset < tlv_array_len)
  {
    type = tlv[0];
    length = tlv[1];
    // CLAIMED OOB precondition at the value-read site:
    CHECKPOINT(value_read_oob, (u16)(offset + 2 + length) > tlv_array_len);
    if(type == LLCP_TLV_MIUX)
      local->remote_miu = (((u16)tlv[2] << 8) | tlv[3]) & 0x7ff; // reads tlv[2..3]
    offset += length + 2;
    tlv += length + 2;
  }
  return 0;
}

// fixed (CVE-2026-31622): bound the element before use.  The same probe at
// the value-read site is now unreachable with the overrun condition.
int llcp_parse_fixed(struct nfc_llcp_local *local, const u8 *tlv_array,
                     u16 tlv_array_len)
{
  const u8 *tlv = tlv_array;
  u16 offset = 0;
  u8 type, length;

  if(local == NULL)
    return -1;

  while(offset + 2 <= tlv_array_len)
  {
    type = tlv[0];
    length = tlv[1];
    if(offset + 2 + (u16)length > tlv_array_len) // FIX: value must fit
      break;
    CHECKPOINT(value_read_oob, (u16)(offset + 2 + length) > tlv_array_len);
    if(type == LLCP_TLV_MIUX)
      local->remote_miu = (((u16)tlv[2] << 8) | tlv[3]) & 0x7ff;
    offset += length + 2;
    tlv += length + 2;
  }
  return 0;
}

#define CAP 8

int probe_vuln(void)
{
  u16 len;
  __CPROVER_assume(len >= 1 && len <= CAP);
  u8 *buf = malloc(len);
  __CPROVER_assume(buf != 0);
  struct nfc_llcp_local local;
  return llcp_parse_vuln(&local, buf, len);
}

int probe_fixed(void)
{
  u16 len;
  __CPROVER_assume(len >= 1 && len <= CAP);
  u8 *buf = malloc(len);
  __CPROVER_assume(buf != 0);
  struct nfc_llcp_local local;
  return llcp_parse_fixed(&local, buf, len);
}
