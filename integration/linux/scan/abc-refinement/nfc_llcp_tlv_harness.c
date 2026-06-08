// CBMC harness for nfc_llcp_parse_gb_tlv / nfc_llcp_parse_connection_tlv
// (net/nfc/llcp_commands.c).
//
// Bug shape: TLV walker with u8 offset vs u16 tlv_array_len.
// Two issues:
//   1. offset is u8, wraps on arrays > 255 -> infinite loop / OOB
//   2. tlv[0]/tlv[1] read without checking offset+2 <= tlv_array_len
//
// Build:
//   goto-cc -o nfc_llcp.gb nfc_llcp_tlv_harness.c
//   cbmc nfc_llcp.gb --function harness_parse_gb_tlv --bounds-check --unwind 140
#include <stdint.h>

#define BUFMAX 260 // just over u8 range to expose the wrap

uint8_t nd_u8(void)
{
  uint8_t x;
  return x;
}
uint16_t nd_u16(void)
{
  uint16_t x;
  return x;
}

// Model of nfc_llcp_parse_gb_tlv (faithful to the real code).
int harness_parse_gb_tlv(void)
{
  uint8_t tlv_array[BUFMAX];
  uint16_t tlv_array_len = nd_u16();
  __CPROVER_assume(tlv_array_len <= BUFMAX);

  const uint8_t *tlv = tlv_array;
  uint8_t type, length, offset = 0;

  while(offset < tlv_array_len)
  {
    type = tlv[0];   // potential OOB: offset could be tlv_array_len - 1
    length = tlv[1]; // potential OOB: offset could be tlv_array_len - 1
    // (switch body elided — doesn't affect bounds)
    offset += length + 2; // u8 wrap if offset+length+2 > 255
    tlv += length + 2;
  }
  return 0;
}

// Fixed version: check header fits + prevent u8 wrap.
int harness_parse_gb_tlv_fixed(void)
{
  uint8_t tlv_array[BUFMAX];
  uint16_t tlv_array_len = nd_u16();
  __CPROVER_assume(tlv_array_len <= BUFMAX);

  const uint8_t *tlv = tlv_array;
  uint16_t offset = 0; // widen to u16 to prevent wrap

  while(offset + 2 <= tlv_array_len) // header must fit
  {
    uint8_t length = tlv[1];
    if(offset + 2 + length > tlv_array_len) // value must fit
      break;
    // (switch body)
    offset += length + 2;
    tlv += length + 2;
  }
  return 0;
}
