// -fms-extensions: a *tagged* struct/union used as an unnamed (anonymous)
// member.  Its members are injected into the enclosing struct and it
// contributes its size.  Used pervasively in the Linux kernel
// (e.g. struct __filename_head embedded in struct filename, whose
// static_asserts require the size to be exact).  The _Static_asserts here
// are checked at conversion time, so this fails to compile unless the
// anonymous tagged member is laid out correctly.
struct head
{
  const char *name;
  int refcnt;
  void *aname;
};

struct filename
{
  struct head; // anonymous tagged-struct member
  const char iname[168];
};

_Static_assert(sizeof(struct head) == 24, "head size");
_Static_assert(sizeof(struct filename) == 192, "filename size");
_Static_assert(
  __builtin_offsetof(struct filename, iname) == 24,
  "iname offset");

// a tagged union as an anonymous member, too
struct u_outer
{
  union inner
  {
    int i;
    char c[4];
  };
  long tail;
};
_Static_assert(sizeof(struct u_outer) == 16, "union-anon size");

int main(void)
{
  struct filename f;
  f.refcnt = 7; // inner field reachable through the anonymous member
  f.name = f.iname;

  struct u_outer u;
  u.i = 3; // inner union field reachable

  return f.refcnt + u.i;
}
