/* The ENA shape: the clause names object_whole() of a POINTER FIELD, and a
   callee writes through a pointer derived from it at a symbolic index. */
struct slot { int a; int b; int bufs[4]; };
struct ring { struct slot *slots; unsigned n; };
static struct slot pool[4];
static struct ring r;
unsigned nd(void) { unsigned v; return v; }

static void callee(struct ring *rr, unsigned idx)
{
	struct slot *s = &rr->slots[idx];   /* derived pointer, symbolic index */
	int *p = s->bufs;
	s->a = 1;                            /* write through the derived pointer */
	p[0] = 2;                            /* and one level deeper */
}

int main(void)
{
	unsigned n = nd(), idx = nd();
	r.slots = pool; r.n = 4;
	if(idx >= 4)
		return 0;
	for(unsigned i = 0; i < n; i++)
		__CPROVER_assigns(i, __CPROVER_object_whole(r.slots))
		__CPROVER_loop_invariant(i <= n)
		__CPROVER_decreases(n - i)
	{
		callee(&r, idx);
	}
	return 0;
}
