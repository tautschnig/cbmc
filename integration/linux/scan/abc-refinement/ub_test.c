// Well-definedness (no-UB) threat obligations BEYOND memory safety.
// Each is a sub-class of the "absence of UB" asset with its OWN dedicated
// CBMC check; the THREAT exists only when an actor controls the operand,
// and the downstream impact routes to a different asset.
#include <stdint.h>

typedef uint32_t u32;
typedef int32_t s32;

u32 nd(void)
{
  u32 x;
  return x;
}

// (1) division by zero -- UB; downstream: oops/trap => A4 availability/DoS.
//     CBMC: --div-by-zero-check
int div_buggy(void)
{
  u32 n = nd();
  return (int)(1000u / n); // n == 0 -> UB
}
int div_fixed(void)
{
  u32 n = nd();
  if(n == 0)
    return -1;
  return (int)(1000u / n);
}

// (2) signed integer overflow -- UB; downstream: often A1 (a wrong
//     size/index) or a silent miscompile.  CBMC: --signed-overflow-check
int mul_buggy(void)
{
  s32 n = (s32)nd();
  return n * 16; // signed overflow for large |n|
}
int mul_fixed(void)
{
  s32 n = (s32)nd();
  if(n > 134217727 || n < -134217728)
    return -1;
  return n * 16;
}

// (3) oversized / negative shift -- UB.  CBMC: --undefined-shift-check
int shl_buggy(void)
{
  u32 k = nd();
  return (int)(1u << k); // k >= 32 -> UB
}
int shl_fixed(void)
{
  u32 k = nd();
  if(k >= 32)
    return -1;
  return (int)(1u << k);
}
