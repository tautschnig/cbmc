// Stage-2 CBMC refinement of the vme_user stage-1 candidates.
//
// Models drivers/staging/vme_user/vme_user.c (Linux 6.12).
// kern_buf is a FIXED PCI_BUF_SIZE (0x20000) allocation, but
// the SLAVE read/write path bounds the copy by the VME *window*
// size (image_size = vme_get_size(resource)), which VME_SET_SLAVE
// sets from user-supplied slave.size and only validates against
// the VME address space (vme_check_window), NOT against
// PCI_BUF_SIZE.  So buffer_from_user/buffer_to_user can copy past
// kern_buf when image_size > PCI_BUF_SIZE.
//
// Constants scaled down for bounded model checking; the STRUCTURE
// (fixed buffer vs window-bounded copy) is preserved.  Real
// PCI_BUF_SIZE = 0x20000.
#include <string.h>

#define BUF 8           // models the fixed PCI_BUF_SIZE kern_buf
#define WIN_MAX 64      // models the (large) max VME window / user buf

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

// SLAVE path: vme_user_write -> buffer_from_user (vme_user.c:172).
// count is clamped against image_size (the window), then
// memcpy(kern_buf + ppos, src, count).  No clamp to size_buf.
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
	// buffer_from_user: image_ptr = kern_buf + *ppos; copy count bytes
	memcpy(kern_buf + ppos, user_src, count);
}

// MASTER path: resource_from_user (vme_user.c:147).  count is
// clamped to size_buf (== PCI_BUF_SIZE == kern_buf size) first.
void harness_resource_from_user(void)
{
	unsigned int count = nd_uint();
	__CPROVER_assume(count <= WIN_MAX);
	if(count > BUF)            // if (count > size_buf) count = size_buf
		count = BUF;
	memcpy(kern_buf, user_src, count);
}
