# use_after_free_generic property module

Targets the **non-refcount-imbalance subset** of the
`use_after_free` CVE category — the existing balance modules
catch refcount UAFs; this catches explicit `kfree` followed
by use AND explicit double-free.

Net new coverage: an estimated 5-7% of the 2023-2026 survey
volume (the UAF subset that doesn't reduce to a refcount
balance issue), plus most of the `double_free_or_unlock`
bucket (109 CVEs, 1.3%).

## Bug class

```c
kfree(p);
p->field = 42;     // BUG: deref of freed pointer

kfree(p);
kfree(p);          // BUG: explicit double-free
```

## Files

Standard layout (`use_after_free_generic.{h,c,cocci}`,
`test_unit.c`, `run.sh`).  Cocci file has three rules:
- `kfree(x); ... x->f` — free-then-deref.
- `kfree(x); ... kfree(x)` — explicit double-free.
- `kvfree(x); ... kvfree(x)` — same for kvfree.

## Scan integration

Adapter contracts `__assert_not_freed(p)` with precondition
`is_freed(p) == 0`.  Direct-call harness vuln/fix shapes via
`-DFIXED`.

NOT in `_PER_FILE_SUPPORTED_MODULES` — auto-instrumenting
every kfree / deref site needs cocci-driven instrumentation.
Cocci is the primary integration path.
