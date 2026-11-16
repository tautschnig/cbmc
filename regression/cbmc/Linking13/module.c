// Translation unit where `struct inner` is only forward-declared, so the
// anonymous union inside `struct ring` gets a different synthetic tag.  The
// struct value built here must use the anonymous-union tag of the kept
// `struct ring` once linked.

struct inner;

struct ring
{
  int id;
  union
  {
    struct inner *ip;
    long raw;
  };
};

int read_id(struct ring *r);

int make_and_check(void)
{
  struct ring r = {1, .ip = 0};
  struct ring s = r;
  __CPROVER_assert(s.ip == 0, "anonymous member read back");
  return read_id(&s);
}
