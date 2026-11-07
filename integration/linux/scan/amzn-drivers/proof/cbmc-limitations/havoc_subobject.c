struct Q { int depth; int *reg; };
struct Dev { int *other; struct Q q[4]; };
static struct Dev dev;
static int x;
int main(void)
{
	dev.other = &x;
	__CPROVER_havoc_object(&dev.q[0]);        /* intent: just q[0] */
	__CPROVER_assert(dev.other == &x, "dev.other survives havoc of dev.q[0]");
	return 0;
}
