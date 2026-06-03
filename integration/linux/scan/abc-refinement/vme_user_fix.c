// Stage-2 CBMC proof of the FIX for the vme_user SLAVE-path OOB.
//
// Models the PATCHED drivers/staging/vme_user/vme_user.c
// buffer_from_user / buffer_to_user, which now clamp the copy
// against size_buf (== PCI_BUF_SIZE == kern_buf size) before the
// copy, mirroring resource_from_user.  Same scaled constants as
// vme_user_refine.c so the two are directly comparable:
//   original (vme_user_refine.c)  -> harness_buffer_from_user FAILS
//   patched  (this file)          -> harness_buffer_from_user OK
#include <string.h>

#define BUF 8      // models the fixed PCI_BUF_SIZE kern_buf
#define WIN_MAX 64 // models the (large) max VME window / user buf

static char kern_buf[BUF];
static char user_src[WIN_MAX];

unsigned int nd_uint(void)
{
  unsigned int x; // nondet under CBMC
  return x;
}
int nd_int(void)
{
  int x;
  return x;
}

// PATCHED SLAVE path: buffer_from_user now clamps count against
// size_buf (BUF) after the caller's window clamp.
void harness_buffer_from_user(void)
{
  unsigned int image_size = nd_uint(); // vme_get_size(): window size
  int ppos = nd_int();
  unsigned int count = nd_uint();
  __CPROVER_assume(image_size >= 1);       // window configured
  __CPROVER_assume(image_size <= WIN_MAX); // window may exceed BUF
  __CPROVER_assume(count <= WIN_MAX);
  // vme_user_write guards (vme_user.c:236-243):
  if(ppos < 0 || (unsigned int)ppos > image_size - 1)
    return;
  if((unsigned int)ppos + count > image_size)
    count = image_size - (unsigned int)ppos;
  // THE FIX (buffer_from_user): clamp to the fixed kern_buf.
  if((unsigned int)ppos >= BUF) // *ppos >= size_buf
    return;
  if(count > BUF - (unsigned int)ppos) // count > size_buf - *ppos
    count = BUF - (unsigned int)ppos;
  // buffer_from_user: image_ptr = kern_buf + *ppos; copy count bytes
  memcpy(kern_buf + ppos, user_src, count);
}

// PATCHED buffer_to_user mirrors the same clamp (read side).
void harness_buffer_to_user(void)
{
  unsigned int image_size = nd_uint();
  int ppos = nd_int();
  unsigned int count = nd_uint();
  __CPROVER_assume(image_size >= 1);
  __CPROVER_assume(image_size <= WIN_MAX);
  __CPROVER_assume(count <= WIN_MAX);
  if(ppos < 0 || (unsigned int)ppos > image_size - 1)
    return;
  if((unsigned int)ppos + count > image_size)
    count = image_size - (unsigned int)ppos;
  if((unsigned int)ppos >= BUF)
    return;
  if(count > BUF - (unsigned int)ppos)
    count = BUF - (unsigned int)ppos;
  memcpy(user_src, kern_buf + ppos, count);
}
