void undefined_fn(void);          /* declared, never defined */

int main(void)
{
	undefined_fn();
	/* a deliberately false assertion AFTER the bodyless call: if the tool
	 * keeps going, this must FAIL; if the path was cut, it reports SUCCESS */
	__CPROVER_assert(0, "downstream: deliberately false, must FAIL");
	return 0;
}
