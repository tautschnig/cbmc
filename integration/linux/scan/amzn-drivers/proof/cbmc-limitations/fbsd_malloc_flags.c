#include <stddef.h>
#include <string.h>
/*
 * FreeBSD kernel malloc(9) under plain CBMC.  The declarations, flag values
 * and the malloc macro are those of sys/sys/malloc.h (_KERNEL).  CBMC attaches
 * its userland malloc(size) and free(ptr) models to these symbols by name and
 * ignores the flags, so M_WAITOK may return NULL and M_ZERO is not honoured
 * unless the macro can zero in place (size and flags both constant).
 */
#define M_NOWAIT 0x0001
#define M_WAITOK 0x0002
#define M_ZERO 0x0100
struct malloc_type
{
  const char *name;
};
struct malloc_type M_DEVBUF[1];
void free(void *addr, struct malloc_type *type);
void *malloc(size_t size, struct malloc_type *type, int flags);
#define malloc(size, type, flags)                                              \
  ({                                                                           \
    void *_malloc_item;                                                        \
    size_t _size = (size);                                                     \
    if(                                                                        \
      __builtin_constant_p(size) && __builtin_constant_p(flags) &&             \
      ((flags)&M_ZERO) != 0)                                                   \
    {                                                                          \
      _malloc_item = malloc(_size, type, (flags) & ~M_ZERO);                   \
      if(((flags)&M_WAITOK) != 0 || _malloc_item != NULL)                      \
        memset(_malloc_item, 0, _size);                                        \
    }                                                                          \
    else                                                                       \
    {                                                                          \
      _malloc_item = malloc(_size, type, flags);                               \
    }                                                                          \
    _malloc_item;                                                              \
  })

int main(void)
{
  // M_WAITOK: malloc(9) sleeps until it succeeds, it never returns NULL
  int *a = malloc(sizeof(int), M_DEVBUF, M_WAITOK);
  *a = 1;

  // M_WAITOK | M_ZERO with constant flags: the macro zeroes without a check
  int *b = malloc(sizeof(int), M_DEVBUF, M_WAITOK | M_ZERO);
  __CPROVER_assert(*b == 0, "M_ZERO, constant flags: zeroed");

  // M_ZERO with flags not known at compile time: malloc itself zeroes
  int flags = M_WAITOK | M_ZERO;
  int *c = malloc(sizeof(int), M_DEVBUF, flags);
  __CPROVER_assert(*c == 0, "M_ZERO, variable flags: zeroed");

  free(a, M_DEVBUF);
  free(b, M_DEVBUF);
  free(c, M_DEVBUF);
  return 0;
}
