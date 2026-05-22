# integer_overflow_in_alloc_size property module

Targets the `integer_overflow` CVE category — **122 CVEs (1.4%
of classified volume in the 2023-2026 kernel CVE survey).**

## Bug class

```c
arr = kmalloc(n * sizeof(*arr), GFP_KERNEL);  // BUG if overflows
```

Fix: `kmalloc_array(n, sizeof(*arr), GFP_KERNEL)` (uses
`check_mul_overflow`) or explicit guard.

## Files

- [`integer_overflow_in_alloc_size.h`](integer_overflow_in_alloc_size.h)
- [`integer_overflow_in_alloc_size.c`](integer_overflow_in_alloc_size.c)
- [`integer_overflow_in_alloc_size.cocci`](integer_overflow_in_alloc_size.cocci)
- [`test_unit.c`](test_unit.c)
- [`run.sh`](run.sh)

## Scan integration

Adapter contracts `__assert_size_safe(n, elem_size)` with
precondition `alloc_size_safe(n, elem_size) == 1`.
Direct-call harness vuln/fix shapes via `-DFIXED`.

NOT in `_PER_FILE_SUPPORTED_MODULES` — auto-instrumenting
every `kmalloc` call site needs cocci-driven instrumentation.
Cocci is the primary integration path.
