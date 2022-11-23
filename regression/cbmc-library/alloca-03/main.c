typedef int *int_ptr;

int main()
{
  int_ptr ptr[2];
  for(int i = 0; i < 2; ++i)
  {
    ptr[i] = __builtin_alloca(sizeof(int));
  }
  *(ptr[0]) = 42;
  *(ptr[1]) = 42;
}
