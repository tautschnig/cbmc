/* __CPROVER_object_whole on a FIELD address: does it stay within the field? */
struct S { int a; int b; };
static struct S s;
int nd(void);

int main(void)
{
	unsigned n = (unsigned)nd();
	unsigned i = 0;

	s.a = 1;
	s.b = 7;

	while(i < n)
		__CPROVER_assigns(i, __CPROVER_object_whole(&s.a))
		__CPROVER_loop_invariant(i <= n)
		__CPROVER_decreases(n - i)
	{
		s.a = 2;
		i++;
	}

	/* s.b is NOT named in the assigns clause, so it must survive the loop */
	__CPROVER_assert(s.b == 7, "s.b is outside the assigns clause");
	return 0;
}
