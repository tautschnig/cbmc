/// \file
/// del_timer_sync_before_free_kernel_direct_harness.c

struct timer_list
{
  int dummy;
};

void timer_set_armed(struct timer_list *t);
void timer_clear_armed(struct timer_list *t);
int timer_armed(struct timer_list *t);

void __assert_no_armed_timer(struct timer_list *t);

struct my_struct
{
  int data;
  struct timer_list t;
};

int main(void)
{
  static struct my_struct x;
  struct my_struct *xp = &x;

  // timer_setup equivalent.
  timer_set_armed(&xp->t);

#ifdef FIXED
  timer_clear_armed(&xp->t);
#endif

  __assert_no_armed_timer(&xp->t);
  return 0;
}
