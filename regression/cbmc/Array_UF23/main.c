int main()
{
  int a[100];
  int i, j, w, v;
  __CPROVER_assume(0 <= i && i < 100 && 0 <= j && j < 100);
  __CPROVER_assume(0 <= w && w < 100);
  a[w] = v;
  _Bool c;
  if(c)
    a[0] = 1;
  int b[100];
  b[w] = a[w];
  __CPROVER_assert(i != j || a[i] == a[j], "equal indices, equal elements");
  __CPROVER_assert(
    i != w || a[i] == v || (c && i == 0), "element written by a with");
  __CPROVER_assert(i != j || b[i] == b[j], "equal indices of a leaf");
  __CPROVER_assert(a[i] == a[j], "different indices may differ");
}
