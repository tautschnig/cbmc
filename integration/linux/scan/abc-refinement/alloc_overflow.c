// CBMC harness for the integer-overflow-in-allocation bug class.
//
// Shape: a user-controlled count/size reaches an allocation-size
// arithmetic expression (n*elem, hdr+len) that can wrap; the wrapped
// (small) allocation is then written with the *intended* (large) size
// -> heap overflow.  CBMC decides the arithmetic exactly.
//
// Two harnesses, same structure, so the proof is meaningful:
//   harness_alloc_overflow_BUG  -> VERIFICATION FAILED  (no overflow guard)
//   harness_alloc_overflow_FIXED-> VERIFICATION SUCCESSFUL (guarded)
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

unsigned int nd_uint(void)
{
  unsigned int x;
  return x;
}

// Models: buf = kmalloc(n * ELEM); ... copy n*ELEM bytes into buf.
// The alloc size (size_t arithmetic) and the copy size use the SAME
// expression, but the alloc computes it in a NARROWER type (u32),
// modelling drivers that do `u32 bytes = n * elem; p = kmalloc(bytes)`
// then copy `(size_t)n * elem`.
#define ELEM 8

void harness_alloc_overflow_BUG(void)
{
  unsigned int n = nd_uint(); // user-controlled element count
  // 32-bit allocation-size arithmetic: can wrap for large n.
  unsigned int alloc_bytes = n * ELEM; // <-- may overflow u32
  char *buf = malloc(alloc_bytes);
  __CPROVER_assume(buf != NULL);
  // The copy uses the full (non-wrapping) intended size.
  size_t copy_bytes = (size_t)n * ELEM;
  // Write one byte at the last intended offset -> OOB iff wrap occurred.
  if(copy_bytes > 0)
    buf[copy_bytes - 1] = 0; // FAILS when alloc_bytes < copy_bytes
}

void harness_alloc_overflow_FIXED(void)
{
  unsigned int n = nd_uint();
  // Guard against the multiplication overflow (the real fix:
  // check_mul_overflow / kmalloc_array / array_size()).
  if(n > (UINT32_MAX / ELEM))
    return;
  unsigned int alloc_bytes = n * ELEM; // now provably no wrap
  char *buf = malloc(alloc_bytes);
  __CPROVER_assume(buf != NULL);
  size_t copy_bytes = (size_t)n * ELEM;
  if(copy_bytes > 0)
    buf[copy_bytes - 1] = 0; // safe: alloc_bytes == copy_bytes
}
