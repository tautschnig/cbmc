// Confidentiality threat template -- kernel->user info leak via
// uninitialised struct padding.  CBMC obligation for the threat:
//   asset   = confidentiality of kernel memory (non-public data)
//   actor   = unprivileged user (receives copy_to_user / netlink / ioctl out)
//   threat  = a byte the user receives carries leftover kernel data
//             (uninitialised field or struct padding)
//   mitigation to prove = every byte of the copied region is initialised
//
// Modelled concretely: an on-stack reply struct has 3 padding bytes after
// `type` (before the 4-byte-aligned `value`).  "copy_to_user" exposes all
// sizeof(struct) bytes; the obligation is that the padding bytes do not
// carry leftover (uninitialised) kernel stack data.
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;

struct reply
{
  u8 type;   // offset 0
  u32 value; // offset 4 -> bytes [1..3] are padding
};

u8 nd(void)
{
  u8 x;
  return x;
}

// model the copy-to-user obligation: the `n` exposed bytes must all be
// initialised (here: padding bytes must not carry leftover data).
static void copy_to_user_obligation(const void *src, unsigned n)
{
  const u8 *b = (const u8 *)src;
  // bytes [1..3] are struct padding -- they must have been written
  // (initialised), modelled as == 0 after a zeroing memset.
  if (n >= 4)
    __CPROVER_assert(b[1] == 0 && b[2] == 0 && b[3] == 0,
                     "no kernel info-leak via uninitialised padding");
}

// BUGGY: fields filled, padding left as leftover kernel stack data.
int reply_buggy(void)
{
  struct reply r;
  r.type = nd();
  r.value = nd();
  copy_to_user_obligation(&r, sizeof(r));
  return 0;
}

// FIXED: memset(0) covers the padding before the fields are filled.
int reply_fixed(void)
{
  struct reply r;
  __builtin_memset(&r, 0, sizeof(r));
  r.type = nd();
  r.value = nd();
  copy_to_user_obligation(&r, sizeof(r));
  return 0;
}
