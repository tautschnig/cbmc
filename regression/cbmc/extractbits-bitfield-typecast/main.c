// Regression test for extractbits assertion failure
// Bug: When extracting bits from a 28-bit c_bit_field being typecast to 32-bit bitvector,
// the assertion "index+width-1 of extractbits must be within the bitvector" would fail.
//
// This test reproduces the scenario where index_as_int + bv_width - 1 >= src_bv.size()
// which happens when typecasting from a smaller bit field to a larger integer type.

#include <assert.h>

struct bitfield_test
{
  unsigned int f28 : 28; // 28 bits
  unsigned int f30 : 30; // 30 bits
  signed int s28 : 28;   // signed 28 bits
};

int main()
{
  struct bitfield_test bf;

  // Test case 1: 28-bit unsigned to 32-bit unsigned
  bf.f28 = 0x0ABCDEF0;
  unsigned int u1 = (unsigned int)bf.f28;
  assert(u1 == 0x0ABCDEF0);

  // Test case 2: 30-bit unsigned to 32-bit unsigned
  bf.f30 = 0x3FFFFFFF;
  unsigned int u2 = (unsigned int)bf.f30;
  assert(u2 == 0x3FFFFFFF);

  // Test case 3: 28-bit signed to 32-bit signed (with sign extension)
  bf.s28 = -1; // All bits set in 28-bit signed
  signed int s1 = (signed int)bf.s28;
  assert(s1 == -1);

  // Test case 4: 28-bit signed positive value
  bf.s28 = 100;
  signed int s2 = (signed int)bf.s28;
  assert(s2 == 100);

  // Test case 5: Boundary values
  bf.f28 = 0;
  unsigned int u3 = (unsigned int)bf.f28;
  assert(u3 == 0);

  bf.f28 = 0x0FFFFFFF; // Max value for 28 bits
  unsigned int u4 = (unsigned int)bf.f28;
  assert(u4 == 0x0FFFFFFF);

  return 0;
}
