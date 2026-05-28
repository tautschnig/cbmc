// Regression test for the fix to:
//   src/solvers/flattening/boolbv_index.cpp
// where indexing into an `extern T arr[]` (incomplete array)
// at multiple indices used to register the symbol with width
// 0 in boolbv_mapt, then trip the size-equals-width invariant
// in get_literals.
//
// The Linux kernel hits this via <linux/ctype.h>'s
// `extern const unsigned char _ctype[]` declaration combined
// with the `__ismask(c)` macro that expands to `_ctype[c]`.

extern const unsigned char _ctype[];

int nondet_int(void);

int main(void)
{
  int c = nondet_int();
  if(c >= 0 && c < 128)
  {
    unsigned char m1 = _ctype[c];
    unsigned char m2 = _ctype[c + 1];
    return m1 + m2;
  }
  return 0;
}
