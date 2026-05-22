# copy_from_user_size_check property module

Targets the `string_or_copy_bound` CVE category — **27 CVEs
(0.3% of classified volume in the 2023-2026 kernel CVE
survey).**  Smaller bucket but high-severity (stack OOB
writes from userspace).

## Bug class

```c
char buf[64];
copy_from_user(buf, user_ptr, user_len);   // BUG if user_len > 64
```

Fix: validate `user_len <= sizeof(buf)` first or clamp via
`min_t(size_t, user_len, sizeof(buf))`.

## Files

- [`copy_from_user_size_check.h`](copy_from_user_size_check.h)
- [`copy_from_user_size_check.c`](copy_from_user_size_check.c)
- [`copy_from_user_size_check.cocci`](copy_from_user_size_check.cocci)
- [`test_unit.c`](test_unit.c)
- [`run.sh`](run.sh)

## Scan integration

Adapter contracts `__assert_copy_safe(dst_cap, len)` with
precondition `copy_len_safe(dst_cap, len) == 1`.  Direct-call
harness vuln/fix shapes via `-DFIXED`.

NOT in `_PER_FILE_SUPPORTED_MODULES` — auto-instrumenting
every `copy_from_user` call site needs cocci-driven
instrumentation.  Cocci is the primary integration path.
