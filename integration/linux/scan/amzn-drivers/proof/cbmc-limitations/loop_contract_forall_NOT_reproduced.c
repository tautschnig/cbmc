/* Does a __CPROVER_forall invariant supplied from a loop-contracts FILE take
   effect?  g[1..3] are never written, so the quantified clause should let the
   assertion pass. */
static int g[4];
unsigned nd(void);

int main(void)
{
	unsigned n = nd();
	unsigned i = 0;
	unsigned k;

	for(k = 0; k < 4; k++)
		g[k] = 0;

	while(i < n)
	{
		g[0] = 0;
		i++;
	}

	__CPROVER_assert(g[1] == 0, "g[1] is never written");
	return 0;
}
