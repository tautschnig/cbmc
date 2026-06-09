// Real-function reach harness for CVE-2019-3701 (CAN gateway OOB write),
// the HIGH/WRITE candidate the closed loop flags as cgw_csum_*.
//
// This is the VERBATIM net/can/gw.c body of cgw_csum_crc8_pos and the
// VERBATIM uapi struct layouts (canfd_frame, cgw_csum_crc8) from
// include/uapi/linux/can{,/gw}.h -- not an abstract shape.  The only added
// line is a single CHECKPOINT() at the sink so cover_probe.py can report
// whether the OOB-write precondition is reachable.  The full translation
// unit also goto-cc's cleanly (see the doc), confirming the C front-end
// already handles this kernel code; the harness merely isolates the
// property from whole-TU nondet-pointer noise.
//
// The CVE: from_idx/to_idx/result_idx are __s8 fields copied raw from a
// netlink attribute (nla_memcpy in cgw_parse_attr); nothing bounds them
// against the frame, so cf->data[result_idx] writes out of the 64-byte
// payload.  The fix validates the indices in the caller.
#include "cover_probe.h"
#include <stdint.h>

typedef uint8_t u8;
typedef int8_t __s8;
typedef uint8_t __u8;
typedef uint32_t canid_t;

#define CANFD_MAX_DLEN 64

struct canfd_frame
{
  canid_t can_id;
  __u8 len;
  __u8 flags;
  __u8 __res0;
  __u8 __res1;
  __u8 data[CANFD_MAX_DLEN] __attribute__((aligned(8)));
};

struct cgw_csum_crc8
{
  __s8 from_idx;
  __s8 to_idx;
  __s8 result_idx;
  __u8 init_crc_val;
  __u8 final_xor_val;
  __u8 crctab[256];
  __u8 profile;
  __u8 profile_data[20];
} __attribute__((packed));

#define CGW_CRC8PRF_1U8 1
#define CGW_CRC8PRF_16U8 2
#define CGW_CRC8PRF_SFFID_XOR 3

// ---- VERBATIM body (net/can/gw.c), + one CHECKPOINT line at the sink ----
static void cgw_csum_crc8_pos(struct canfd_frame *cf,
                              struct cgw_csum_crc8 *crc8)
{
  u8 crc = crc8->init_crc_val;
  int i;

  for(i = crc8->from_idx; i <= crc8->to_idx; i++)
    crc = crc8->crctab[crc ^ cf->data[i]];

  switch(crc8->profile)
  {
  case CGW_CRC8PRF_1U8:
    crc = crc8->crctab[crc ^ crc8->profile_data[0]];
    break;

  case CGW_CRC8PRF_16U8:
    crc = crc8->crctab[crc ^ crc8->profile_data[cf->data[1] & 0xF]];
    break;

  case CGW_CRC8PRF_SFFID_XOR:
    crc = crc8->crctab[crc ^ (cf->can_id & 0xFF) ^ (cf->can_id >> 8 & 0xFF)];
    break;
  }

  // OOB-write precondition at the sink (the only non-verbatim line):
  CHECKPOINT(oob, crc8->result_idx < 0 || crc8->result_idx >= CANFD_MAX_DLEN);
  cf->data[crc8->result_idx] = crc ^ crc8->final_xor_val;
}

unsigned char nd(void)
{
  unsigned char x;
  return x;
}

// vulnerable: indices are raw attacker bytes (as nla_memcpy delivers them).
// from_idx/to_idx fixed to 0 to keep the read loop trivial; the result_idx
// WRITE -- the actual primitive -- is fully nondet.
int probe_vuln(void)
{
  struct canfd_frame cf;
  struct cgw_csum_crc8 crc8;
  crc8.from_idx = 0;
  crc8.to_idx = 0;
  crc8.profile = 0;
  crc8.result_idx = (__s8)nd(); // attacker-controlled, unvalidated
  cgw_csum_crc8_pos(&cf, &crc8);
  return cf.data[0];
}

// fixed: the caller validates the index against the frame (0..len), as the
// CVE fix does.  result_idx is constrained, so the sink is in-bounds.
int probe_fixed(void)
{
  struct canfd_frame cf;
  struct cgw_csum_crc8 crc8;
  crc8.from_idx = 0;
  crc8.to_idx = 0;
  crc8.profile = 0;
  crc8.result_idx = (__s8)nd();
  __CPROVER_assume(crc8.result_idx >= 0 && crc8.result_idx < CANFD_MAX_DLEN);
  cgw_csum_crc8_pos(&cf, &crc8);
  return cf.data[0];
}
