long A[6];
int B[7];

int main()
{
  // An update of n bytes (of symbolic number) at offset k of an array of
  // elements wider than a byte: each byte of the range, and only those, is
  // updated.
  unsigned long n, k, i;
  __CPROVER_assume(n >= 1 && n <= 40 && k <= 48 && k + n <= 48 && i < 48);
  unsigned char buf[n];
  __CPROVER_array_set(buf, 1);
  __CPROVER_array_replace((unsigned char *)A + k, buf);
  unsigned char *bytes = (unsigned char *)A;
  __CPROVER_assert(bytes[i] == ((k <= i && i < k + n) ? 1 : 0), "long exact");

  unsigned long m, j, l;
  __CPROVER_assume(m >= 1 && m <= 28 && l <= 28 && l + m <= 28 && j < 28);
  unsigned char buf2[m];
  __CPROVER_array_set(buf2, 2);
  __CPROVER_array_replace((unsigned char *)B + l, buf2);
  unsigned char *bytes2 = (unsigned char *)B;
  __CPROVER_assert(bytes2[j] == ((l <= j && j < l + m) ? 2 : 0), "int exact");
  return 0;
}
