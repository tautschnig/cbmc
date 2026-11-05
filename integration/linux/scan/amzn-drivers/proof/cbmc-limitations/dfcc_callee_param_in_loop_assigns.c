/* As callee_locals.c, but the callee contains its own (uncontracted) loop,
   which is what ena_tx_map_mbuf does: it walks an mbuf ->next chain while
   writing its locals.  The outer loop carries a contract; the inner one is
   left to be unrolled. */
struct node { int val; struct node *next; };
static struct node chain[4];
static int g[4];

static void callee(struct node *m, int v)
{
	int acc = 0;
	int *q = &acc;

	while((m = m->next) != 0)   /* the callee's own loop */
	{
		acc += m->val;          /* writes a callee local */
		*q = acc;
	}
	g[0] = acc;
}

unsigned nd(void) { unsigned v; return v; }

int main(void)
{
	unsigned n = nd();
	for(int k = 0; k < 3; k++)
		chain[k].next = &chain[k + 1];
	chain[3].next = 0;

	for(unsigned i = 0; i < n; i++)
		__CPROVER_assigns(i, __CPROVER_object_whole(g))
		__CPROVER_loop_invariant(i <= n)
		__CPROVER_decreases(n - i)
	{
		callee(&chain[0], (int)i);
	}
	return 0;
}
