// CBMC harness variants for broadened loop oracle:
// (a) write-OOB: memcpy INTO a fixed buffer inside a TLV loop
// (b) length-underflow: len-1 when len can be 0 -> wraps to huge value
//
// Build: goto-cc -o x.gb loop_oracle_variants.c
//   cbmc x.gb --function harness_write_oob_buggy --bounds-check --unwind 10
//   cbmc x.gb --function harness_underflow_buggy --bounds-check --unsigned-overflow-check --unwind 10
#include <stdint.h>
#include <string.h>

#define BUFMAX 8
#define DEST_SIZE 4

uint16_t nd_u16(void)
{
  uint16_t x;
  return x;
}

// (a) write-OOB: copy a TLV element's value into a FIXED dest buffer
// without bounding the element's declared length against dest size.
// Shape: while (off < total) { len = buf[off+1]; memcpy(dest, &buf[off+2], len); ... }
int harness_write_oob_buggy(void)
{
  uint8_t buf[BUFMAX];
  uint8_t dest[DEST_SIZE]; // fixed output buffer
  uint16_t total = nd_u16();
  __CPROVER_assume(total <= BUFMAX);

  uint16_t off = 0;
  while(off + 2 <= total)
  {
    uint8_t len = buf[off + 1];
    if(off + 2 + len > total)
      break; // element fits in input (read-side is fine)
    // BUG: copy into fixed dest without bounding len against DEST_SIZE
    memcpy(dest, &buf[off + 2], len); // <-- write-OOB when len > DEST_SIZE
    off += 2 + len;
  }
  return 0;
}

int harness_write_oob_fixed(void)
{
  uint8_t buf[BUFMAX];
  uint8_t dest[DEST_SIZE];
  uint16_t total = nd_u16();
  __CPROVER_assume(total <= BUFMAX);

  uint16_t off = 0;
  while(off + 2 <= total)
  {
    uint8_t len = buf[off + 1];
    if(off + 2 + len > total)
      break;
    if(len > DEST_SIZE) // FIX: bound against dest
      break;
    memcpy(dest, &buf[off + 2], len);
    off += 2 + len;
  }
  return 0;
}

// (b) length-underflow: len-1 used as a size when len can be 0.
// Shape: service_name_len = length - 1; memcpy/strncmp(buf, ptr, service_name_len)
// Real example: nfc_llcp_recv_snl SDREQ case.
int harness_underflow_buggy(void)
{
  uint8_t buf[BUFMAX];
  uint16_t total = nd_u16();
  __CPROVER_assume(total <= BUFMAX);

  uint16_t off = 0;
  while(off + 2 <= total)
  {
    uint8_t length = buf[off + 1];
    if(off + 2 + length > total)
      break;
    // BUG: subtract 1 from length without checking length >= 1
    uint8_t svc_len = length - 1; // wraps to 255 when length==0
    uint8_t acc = 0;
    for(uint8_t i = 0; i < svc_len; i++)
      acc += buf[off + 3 + i]; // OOB read with svc_len=255
    off += 2 + length;
  }
  return 0;
}

int harness_underflow_fixed(void)
{
  uint8_t buf[BUFMAX];
  uint16_t total = nd_u16();
  __CPROVER_assume(total <= BUFMAX);

  uint16_t off = 0;
  while(off + 2 <= total)
  {
    uint8_t length = buf[off + 1];
    if(off + 2 + length > total)
      break;
    if(length < 1) // FIX: guard against underflow
      break;
    uint8_t svc_len = length - 1;
    uint8_t acc = 0;
    for(uint8_t i = 0; i < svc_len; i++)
      acc += buf[off + 3 + i];
    off += 2 + length;
  }
  return 0;
}
