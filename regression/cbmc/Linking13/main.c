// Translation unit where `struct inner` is complete when `struct ring` is
// declared, so the anonymous union inside `struct ring` gets a synthetic tag
// that differs from the one module.c gives it.  This translation unit is
// linked first, so its `struct ring` (and its anonymous-union tag) is kept.

struct inner
{
  int x;
};

struct ring
{
  int id;
  union
  {
    struct inner *ip;
    long raw;
  };
};

int make_and_check(void);

int read_id(struct ring *r)
{
  return r->id;
}

int main(void)
{
  __CPROVER_assert(make_and_check() == 1, "struct value built in module.c");
  return 0;
}
