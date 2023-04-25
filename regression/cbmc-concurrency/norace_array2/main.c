__CPROVER_bool __CPROVER_threads_exited[__CPROVER_constant_infinity_uint];
__CPROVER_thread_local unsigned long __CPROVER_thread_id = 0;
unsigned long __CPROVER_next_thread_id = 0;

void __spawned_thread(
  unsigned long this_thread_id,
  void (*start_routine)(void))
{
  __CPROVER_thread_id = this_thread_id;
  start_routine();
  __CPROVER_threads_exited[this_thread_id] = 1;
}

int create(
  unsigned long *thread,              // must not be null
  void (*start_routine)(void))
{
  unsigned long this_thread_id;
  __CPROVER_atomic_begin();
  this_thread_id=++__CPROVER_next_thread_id;
  __CPROVER_atomic_end();

  *thread=this_thread_id;

  __CPROVER_ASYNC_1:
    __spawned_thread(
      this_thread_id,
      start_routine);

    return 0;
}

unsigned long data[2];

void thread0(void)
{
    data[0] = 1;
}

void thread1(void)
{
    data[1] = 1;
}

int main()
{
    unsigned long t0;
    unsigned long t1;

    create(&t0, thread0);
    create(&t1, thread1);
    __CPROVER_assume(__CPROVER_threads_exited[t0]);
    __CPROVER_assume(__CPROVER_threads_exited[t1]);

    __CPROVER_assert(data[0] == 1, "");
    __CPROVER_assert(data[1] == 1, "");

    return 0;
}
