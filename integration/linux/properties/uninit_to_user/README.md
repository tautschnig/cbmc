# uninit_to_user property module

Targets `uninit_or_info_leak` (44 CVEs, 0.5%).  Concrete
recent: CVE-2026-22978 (wifi: avoid kernel-infoleak from
struct iw_point).

## Bug class

```c
struct kernel_info info;
info.field_a = ...;
// forgot info.field_b
copy_to_user(user_ptr, &info, sizeof(info));   // BUG: leaks
                                                // uninit field_b
```

Fix: `memset(&info, 0, sizeof(info));` first, or initialise
every field.

## Files

Standard layout.  Cocci finds `T x; ... copy_to_user(_, &x,
sizeof(x))` and `nla_put(_, _, sizeof(x), &x)` without
intervening `memset` or designated initialiser.

## Scan integration

Adapter contracts `__assert_safe_for_userspace(p)` with
precondition `is_initialised(p) == 1`.  Direct-call harness
vuln/fix shapes via `-DFIXED`.

NOT in `_PER_FILE_SUPPORTED_MODULES`.
