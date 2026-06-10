// Availability threat template (A4).  CBMC obligations:
//   asset   = availability (bounded work / resource on attacker input)
//   actor   = attacker controlling a loop bound or allocation size
//   threat  = unbounded loop (CPU DoS) or unbounded allocation (memory DoS)
//   mitigation to prove =
//       (a) the loop terminates within K iterations  -- CBMC unwinding
//           assertions (the loop is bounded by a clamp), and
//       (b) the allocation size is <= K               -- explicit bound.
//
// Note: these may be the SAME sites the memory-safety oracles flag, but the
// OBLIGATION is different (termination / size<=K, not bounds / no-overflow)
// -- which is exactly the threat-model point.
#include <stdint.h>

typedef uint32_t u32;

u32 nd(void)
{
  u32 x;
  return x;
}

// model an allocator that requires a bounded request
static void *kmalloc_model(u32 sz)
{
  __CPROVER_assert(sz <= 4096, "allocation size is bounded (no memory DoS)");
  return (void *)0;
}

// (a) loop termination -- run under: cbmc --unwind 9 --unwinding-assertions
int loop_buggy(void)
{
  u32 n = nd();
  u32 s = 0;
  for(u32 i = 0; i < n; i++) // unbounded: n is attacker-controlled
    s += i;
  return (int)s;
}
int loop_fixed(void)
{
  u32 n = nd();
  if(n > 8) // clamp -> terminates within 8
    return -1;
  u32 s = 0;
  for(u32 i = 0; i < n; i++)
    s += i;
  return (int)s;
}

// (b) allocation size bound
int alloc_buggy(void)
{
  u32 n = nd();
  return kmalloc_model(n * 16u) != 0; // unbounded size
}
int alloc_fixed(void)
{
  u32 n = nd();
  if(n > 64) // clamp -> size <= 1024
    return -1;
  return kmalloc_model(n * 16u) != 0;
}
