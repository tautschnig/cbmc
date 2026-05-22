# null_after_alloc property module

Targets the `null_pointer_deref` CVE category — **the largest
single bug-shape category in the 2023-2026 kernel CVE survey
(1,035 CVEs, 11.9% of classified volume).**

## Bug class

```c
struct foo *p = kmalloc(sizeof(*p), GFP_KERNEL);
p->field = 42;     // BUG: NULL deref if alloc failed
```

The fix is `if (!p) return -ENOMEM;` between alloc and deref.

## Files

- [`null_after_alloc.h`](null_after_alloc.h) — public API.
- [`null_after_alloc.c`](null_after_alloc.c) — reference impl.
- [`test_unit.c`](test_unit.c) — unit tests.
- [`null_after_alloc.cocci`](null_after_alloc.cocci) — finds
  kmalloc / kzalloc / kcalloc / alloc_skb dereferences without
  intervening NULL checks.
- [`run.sh`](run.sh) — regression runner.

## Scan integration

Adapter contracts a synthetic `__assert_safe_to_deref(p)`
checkpoint with precondition
`p != NULL || null_check_done(p) == 1`.  Direct-call harness:
- Vuln: allocator may return NULL; harness skips the check;
  precondition fires.
- Fix (`-DFIXED`): explicit `if (!p) return -1` plus
  `assert_null_check_done(p)` after; precondition holds.

NOT in `_PER_FILE_SUPPORTED_MODULES` — instrumenting every
deref site needs cocci-driven instrumentation or a
goto-instrument pass.  Cocci is the primary integration
path.
