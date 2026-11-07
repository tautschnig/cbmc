/* The FreeBSD shape: a struct field reached through a pointer parameter, with
   a sibling POINTER field that must survive. */
struct Inner { int x; int y; };
struct Ring { struct Inner stats; int *sq; int ntc; };
static struct Ring r;
static int sq_obj;
unsigned nd(void);

static void op(struct Ring *ring)
{
	unsigned n = nd();
	unsigned i = 0;

	while(i < n)
		__CPROVER_assigns(i, ring->ntc, __CPROVER_object_whole(&ring->stats))
		__CPROVER_loop_invariant(i <= n)
		__CPROVER_decreases(n - i)
	{
		ring->stats.x = 1;
		ring->ntc = 2;
		i++;
	}

	/* ring->sq is not in the assigns clause and must survive */
	__CPROVER_assert(ring->sq == &sq_obj, "ring->sq survives the loop");
}

int main(void)
{
	r.sq = &sq_obj;
	r.ntc = 0;
	op(&r);
	return 0;
}
