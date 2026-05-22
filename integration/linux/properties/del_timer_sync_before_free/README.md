# del_timer_sync_before_free property module

Sibling of `cancel_work_before_free` for
`struct timer_list`.  Same shape; targets the
cancel-before-free UAF bug class on kernel timers.

## Bug class

```c
struct foo *x = kmalloc(sizeof(*x), GFP_KERNEL);
timer_setup(&x->t, callback, 0);
mod_timer(&x->t, jiffies + HZ);
...
kfree(x);   // BUG: timer fires against freed memory
```

The fix is `del_timer_sync(&x->t)` (or `timer_delete_sync`
in the 6.x API rename) before the kfree.

## Files

- [`del_timer_sync_before_free.h`](del_timer_sync_before_free.h)
- [`del_timer_sync_before_free.c`](del_timer_sync_before_free.c)
- [`test_unit.c`](test_unit.c)
- [`del_timer_sync_before_free.cocci`](del_timer_sync_before_free.cocci)
- [`run.sh`](run.sh)

## Scan integration

Mirrors `cancel_work_before_free` exactly: synthetic
`__assert_no_armed_timer(t)` checkpoint with precondition
`timer_armed(t) == 0`.  Vuln/fix shapes via `-DFIXED`.

See the Phase 2 cancel_work_before_free README for the broader
discussion of this module class's limitations.
