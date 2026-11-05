/* A contract-carrying loop calls a function that writes its OWN locals.
   Those locals are not part of the caller's frame, so they should not need
   to appear in the loop's assigns clause. */
static int g[4];

static void callee(int v)
{
	int local = v;        /* a local of the callee */
	int *p = &local;      /* and a local pointer */
	*p = v + 1;
	g[0] = *p;            /* the only caller-visible effect */
}

unsigned nd(void) { unsigned v; return v; }

int main(void)
{
	unsigned n = nd();
	for(unsigned i = 0; i < n; i++)
		__CPROVER_assigns(i, __CPROVER_object_whole(g))
		__CPROVER_loop_invariant(i <= n)
		__CPROVER_decreases(n - i)
	{
		callee((int)i);
	}
	return 0;
}
