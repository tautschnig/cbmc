// Real-context (interprocedural) harness: eliminates the caller-context
// false positive that the scaled intraprocedural model produced.
//
// The sharpened query flagged create_area_user_writer ->
// copy_from_user(ap->buf, buf, count) into a fixed buf[MAX_DATA_AREA_SZ].
// The scaled intraprocedural harness reports FAILED (count unbounded
// locally); the real code is SAFE because the CALLER anybuss_write_input
// clamps: len = min_t(loff_t, MAX_DATA_AREA_SZ - *offset, size).
//
// Here we model the REAL call chain with the REAL constant
// (MAX_DATA_AREA_SZ = 0x200 = 512), so CBMC reasons about the caller's
// precondition -- turning the intraprocedural FP into either a proof or
// a precise reachability question.
//
//   harness_chain_vfs   (*offset >= 0, the vfs precondition) -> SUCCESSFUL
//   harness_chain_nopre (*offset unconstrained)              -> FAILED
//
// The contrast is the point: the safety depends precisely on *offset>=0,
// which vfs guarantees for write(); the scaled model couldn't see this.
#include <stdint.h>
#include <string.h>

#define MAX_DATA_AREA_SZ 0x200 // real constant from host.c

static unsigned char area_buf[MAX_DATA_AREA_SZ]; // models ap->buf[]
static unsigned char user_src[4096];             // models the __user buffer

long nd_long(void)
{
  long x;
  return x;
}
unsigned long nd_ulong(void)
{
  unsigned long x;
  return x;
}

// callee: create_area_user_writer's copy (count bytes into ap->buf).
static void create_area_user_writer(unsigned long count)
{
  // copy_from_user(ap->buf, buf, count) modelled as a bounded write.
  memcpy(area_buf, user_src, count);
}

// caller: anybuss_write_input, with the real min_t clamp.
static long anybuss_write_input(unsigned long size, long offset)
{
  // ssize_t len = min_t(loff_t, MAX_DATA_AREA_SZ - *offset, size);
  long cap = (long)MAX_DATA_AREA_SZ - offset;
  long len = ((long)size < cap) ? (long)size : cap;
  if(len <= 0)
    return 0;
  create_area_user_writer((unsigned long)len);
  return len;
}

// Entry with the vfs precondition (write() guarantees ppos >= 0).
void harness_chain_vfs(void)
{
  unsigned long size = nd_ulong();
  long offset = nd_long();
  __CPROVER_assume(offset >= 0); // vfs rw_verify_area: non-negative ppos
  __CPROVER_assume(size <= 4096);
  anybuss_write_input(size, offset);
}

// Entry WITHOUT the precondition: shows the exact dependency.
void harness_chain_nopre(void)
{
  unsigned long size = nd_ulong();
  long offset = nd_long();
  __CPROVER_assume(size <= 4096);
  anybuss_write_input(size, offset);
}
