# cancel_delayed_work_before_free property module

Sibling of `cancel_work_before_free` for
`struct delayed_work`.  Same shape; targets the
cancel-before-free UAF bug class on timer-driven work-queue
items.

## Bug class

```c
struct foo *x = kmalloc(sizeof(*x), GFP_KERNEL);
INIT_DELAYED_WORK(&x->dwork, worker);
schedule_delayed_work(&x->dwork, HZ);
...
kfree(x);   // BUG: pending delayed work runs against freed memory
```

The fix is `cancel_delayed_work_sync(&x->dwork)` before the
kfree.

## Files

- [`cancel_delayed_work_before_free.h`](cancel_delayed_work_before_free.h)
- [`cancel_delayed_work_before_free.c`](cancel_delayed_work_before_free.c)
- [`test_unit.c`](test_unit.c)
- [`cancel_delayed_work_before_free.cocci`](cancel_delayed_work_before_free.cocci)
- [`run.sh`](run.sh)

## Scan integration

Mirrors `cancel_work_before_free` exactly: synthetic
`__assert_no_pending_dwork(d)` checkpoint with precondition
`cancel_dwork_pending(d) == 0`.  Vuln/fix shapes via
`-DFIXED`.

See the Phase 2 cancel_work_before_free README for the broader
discussion of this module class's limitations (no per-file
synthesis on real kernel functions; cocci is the primary
integration path; etc.).
