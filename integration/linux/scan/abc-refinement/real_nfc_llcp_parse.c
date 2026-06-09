// Real-function CBMC harness: nfc_llcp_parse_gb_tlv (CVE-2026-31622).
//
// This is NOT an abstract shape harness -- it is the ACTUAL function body
// and the ACTUAL llcp_tlv8/llcp_tlv16 helper reads, copied verbatim from
// net/nfc/llcp_commands.c (linux-next next-20260605), with only the
// kernel types/helpers stubbed to their real byte-level behaviour.  The
// buffer is modelled as exactly `tlv_array_len` valid bytes (malloc), so
// CBMC's bounds-check adjudicates the real parser logic.
//
//   goto-cc -o h.gb real_nfc_llcp_parse.c
//   cbmc h.gb --function harness_buggy --bounds-check --pointer-check --unwind 6
//   cbmc h.gb --function harness_fixed --bounds-check --pointer-check --unwind 6
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;

#define LLCP_TLV_VERSION 0x1
#define LLCP_TLV_MIUX 0x2
#define LLCP_TLV_WKS 0x3
#define LLCP_TLV_LTO 0x4
#define LLCP_TLV_OPT 0x7

// real per-type length table (net/nfc/llcp_commands.c), indexed by tlv[0]
static const u8 llcp_tlv_length[256] = {
  0,
  1 /*VER*/,
  2 /*MIUX*/,
  2 /*WKS*/,
  1 /*LTO*/,
  1 /*RW*/,
  0 /*SN*/,
  1 /*OPT*/,
  0 /*SDREQ*/,
  2 /*SDRES*/,
};

struct nfc_llcp_local
{
  u8 remote_version;
  u16 remote_miu;
  u16 remote_lto;
  u8 remote_opt;
  u16 remote_wks;
};

static u16 be16(const u8 *p)
{
  return ((u16)p[0] << 8) | p[1];
}

// ---- REAL helper bodies (verbatim byte reads) --------------------------
static u8 llcp_tlv8(const u8 *tlv, u8 type)
{
  if(tlv[0] != type || tlv[1] != llcp_tlv_length[tlv[0]])
    return 0;
  return tlv[2]; // reads tlv[2]
}
static u16 llcp_tlv16(const u8 *tlv, u8 type)
{
  if(tlv[0] != type || tlv[1] != llcp_tlv_length[tlv[0]])
    return 0;
  return be16(tlv + 2); // reads tlv[2], tlv[3]
}
static u8 llcp_tlv_version(const u8 *tlv)
{
  return llcp_tlv8(tlv, LLCP_TLV_VERSION);
}
static u16 llcp_tlv_miux(const u8 *tlv)
{
  return llcp_tlv16(tlv, LLCP_TLV_MIUX) & 0x7ff;
}
static u16 llcp_tlv_wks(const u8 *tlv)
{
  return llcp_tlv16(tlv, LLCP_TLV_WKS);
}
static u8 llcp_tlv_lto(const u8 *tlv)
{
  return llcp_tlv8(tlv, LLCP_TLV_LTO);
}
static u8 llcp_tlv_opt(const u8 *tlv)
{
  return llcp_tlv8(tlv, LLCP_TLV_OPT);
}

// ---- REAL function body (verbatim) -------------------------------------
static int nfc_llcp_parse_gb_tlv(
  struct nfc_llcp_local *local,
  const u8 *tlv_array,
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

    switch(type)
    {
    case LLCP_TLV_VERSION:
      local->remote_version = llcp_tlv_version(tlv);
      break;
    case LLCP_TLV_MIUX:
      local->remote_miu = llcp_tlv_miux(tlv) + 128;
      break;
    case LLCP_TLV_WKS:
      local->remote_wks = llcp_tlv_wks(tlv);
      break;
    case LLCP_TLV_LTO:
      local->remote_lto = llcp_tlv_lto(tlv) * 10;
      break;
    case LLCP_TLV_OPT:
      local->remote_opt = llcp_tlv_opt(tlv);
      break;
    default:
      break;
    }

    offset += length + 2;
    tlv += length + 2;
  }
  return 0;
}

// ---- the CVE-2026-31622 FIX: bound each element before use -------------
static int nfc_llcp_parse_gb_tlv_fixed(
  struct nfc_llcp_local *local,
  const u8 *tlv_array,
  u16 tlv_array_len)
{
  const u8 *tlv = tlv_array;
  u16 offset = 0; // fix also widens offset to avoid u8 wrap
  u8 type, length;

  if(local == NULL)
    return -1;

  while(offset + 2 <= tlv_array_len)
  { // header must fit
    type = tlv[0];
    length = tlv[1];
    if(offset + 2 + (u16)length > tlv_array_len) // value must fit
      break;
    switch(type)
    {
    case LLCP_TLV_VERSION:
      local->remote_version = llcp_tlv_version(tlv);
      break;
    case LLCP_TLV_MIUX:
      local->remote_miu = llcp_tlv_miux(tlv) + 128;
      break;
    case LLCP_TLV_WKS:
      local->remote_wks = llcp_tlv_wks(tlv);
      break;
    case LLCP_TLV_LTO:
      local->remote_lto = llcp_tlv_lto(tlv) * 10;
      break;
    case LLCP_TLV_OPT:
      local->remote_opt = llcp_tlv_opt(tlv);
      break;
    default:
      break;
    }
    offset += length + 2;
    tlv += length + 2;
  }
  return 0;
}

#define CAP 8

int harness_buggy(void)
{
  u16 len;
  __CPROVER_assume(len >= 1 && len <= CAP);
  u8 *buf = malloc(len); // exactly `len` valid bytes (like the skb gb)
  __CPROVER_assume(buf != 0);
  struct nfc_llcp_local local;
  return nfc_llcp_parse_gb_tlv(&local, buf, len);
}

int harness_fixed(void)
{
  u16 len;
  __CPROVER_assume(len >= 1 && len <= CAP);
  u8 *buf = malloc(len);
  __CPROVER_assume(buf != 0);
  struct nfc_llcp_local local;
  return nfc_llcp_parse_gb_tlv_fixed(&local, buf, len);
}
