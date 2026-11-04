#include <stdlib.h>
#include <string.h>
struct ring { union { unsigned short *tx; unsigned short *rx; }; unsigned size; };
struct adapter { struct ring rings[2]; };
static unsigned short pool[8];
static void *alloc_stub(unsigned long size)      /* the allocator stub shape */
{
	if (size == sizeof(pool)) { memset(pool, 0, sizeof(pool)); return pool; }
	return 0;
}
static int setup(struct adapter *a, unsigned n)  /* the driver shape */
{
	struct ring *r = &a->rings[0];
	r->size = n;
	r->tx = alloc_stub(sizeof(unsigned short) * n);
	if (!r->tx)
		return -1;
	for (unsigned i = 0; i < n; i++)
		r->tx[i] = i;
	return 0;
}
unsigned short nd(void) { unsigned short v; return v; }
int main(void)
{
	struct adapter a;
	unsigned short probe = nd();
	if (setup(&a, 8) == 0 && probe < 8)
		__CPROVER_assert(a.rings[0].tx[probe] == probe, "init loop filled slot");
	return 0;
}
