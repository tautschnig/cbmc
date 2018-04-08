
int flag[2];
int turn;
int one_critical;
int two_critical;

void* observer(void* arg)
{
	__CPROVER_atomic_begin();
	assert(one_critical + two_critical <=1);
	__CPROVER_atomic_end();
}
void* t1(void* arg)
{
	flag[0]=1;
	turn=1;
	__CPROVER_assume(!(flag[1] && turn==1));
	one_critical=1;
	one_critical=0;
	flag[0]=0;
}

void* t2(void* arg)
{
	flag[1]=1;
	turn=0;
	__CPROVER_assume(!(flag[0] && turn==0));
	two_critical=1;
	two_critical=0;
	flag[1]=0;
}


int main()
{
	flag[0]=flag[1]=0;
	turn=0;
	one_critical=two_critical=0;

__CPROVER_ASYNC_0: t1(0);
__CPROVER_ASYNC_1: t2(0);
      observer(0);
	return 0;
}
