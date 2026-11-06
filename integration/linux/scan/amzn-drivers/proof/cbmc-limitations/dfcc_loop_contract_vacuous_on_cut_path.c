static int g;
void helper(int v);          /* body removed below */
int main(void)
{
	unsigned n; unsigned i = 0;
	while(i < n)
		__CPROVER_assigns(i, g)
		__CPROVER_loop_invariant(i <= n)
		__CPROVER_decreases(n - i)
	{
		g = 1;
		helper(g);           /* body removed -> assert(false); assume(false) */
		i++;
	}
	return 0;
}
void helper(int v) { (void)v; }
