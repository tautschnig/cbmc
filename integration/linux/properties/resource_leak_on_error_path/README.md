# resource_leak_on_error_path property module

Targets the `resource_leak` CVE category — **620 CVEs (7.1%
of classified volume in the 2023-2026 kernel CVE survey).**

## Bug class

```c
int handler(...) {
    void *buf = kmalloc(N, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    ret = something_that_may_fail();
    if (ret < 0)
        return ret;     // BUG: leaks `buf`

    ...
    kfree(buf);
    return 0;
}
```

Concrete recent CVEs: CVE-2026-43457 (mctp i2c skb leak),
CVE-2026-43432 (xhci slot disable), CVE-2026-43445 (e1000
DMA error cleanup).

## Files

- [`resource_leak_on_error_path.h`](resource_leak_on_error_path.h)
- [`resource_leak_on_error_path.c`](resource_leak_on_error_path.c)
- [`resource_leak_on_error_path.cocci`](resource_leak_on_error_path.cocci)
  — matches `kmalloc`/`kzalloc` with intervening `return`
  before any `kfree`/`kvfree`; also `alloc_skb` /
  `kfree_skb`.
- [`test_unit.c`](test_unit.c)
- [`run.sh`](run.sh)

## Scan integration

Adapter contracts a synthetic `__assert_no_leak_at_exit(p)`
checkpoint with precondition `leak_outstanding(p) == 0`.
Direct-call harness vuln/fix shapes via `-DFIXED`.

NOT in `_PER_FILE_SUPPORTED_MODULES` — auto-instrumenting
every `return` site needs cocci-driven instrumentation.
Cocci is the primary integration path.
