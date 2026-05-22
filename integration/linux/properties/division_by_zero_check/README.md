# division_by_zero_check property module

Targets the division-by-zero subset of `dos_panic_warn` (249
CVEs, 2.9% of survey volume).  Concrete recent example:
CVE-2026-43354 (iio proximity hx9023s).

## Bug class

```c
rate = total_jiffies / hz_value;   // BUG if hz_value == 0
```

Fix: validate `divisor != 0` before the division.

## Files

Standard layout.  Cocci finds `a / b`, `a % b`, and the
kernel division helpers (`do_div`, `div_u64`, etc.) where
`b` is variable.

## Scan integration

Adapter contracts `__assert_divisor_safe(d)` with
precondition `divisor_safe(d) == 1`.  Direct-call harness
vuln/fix shapes via `-DFIXED`.

NOT in `_PER_FILE_SUPPORTED_MODULES` — instrumenting every
division site needs cocci-driven instrumentation.
