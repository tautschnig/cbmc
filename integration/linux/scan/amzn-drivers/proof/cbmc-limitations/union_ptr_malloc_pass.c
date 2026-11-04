#include <stdlib.h>
struct ring {
	union {            /* anonymous union, as in struct ena_ring */
		int *tx;
		long *rx;
	};
};
int main(void)
{
	struct ring r;
	r.tx = malloc(4 * sizeof(int));
	if (r.tx == 0)
		return 0;
	r.tx[0] = 7;
	__CPROVER_assert(r.tx[0] == 7, "readback through union-held pointer");
	return 0;
}
