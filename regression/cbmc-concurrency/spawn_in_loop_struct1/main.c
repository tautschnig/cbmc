struct fv
{
  void *code;
  void *env;
};

int x;

void t(void *p)
{
  x = 1;
}

int main()
{
  for(int i = 0; i < 2; i++)
  {
    // A struct local declared in the loop that spawns the threads: each
    // thread gets a copy of the current instance only.
    struct fv f = {0, &x};
    void *e = f.env;
  __CPROVER_ASYNC_1:
    t(e);
  }
  __CPROVER_assert(x <= 1, "x");
}
