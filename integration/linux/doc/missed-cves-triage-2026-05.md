# Triage of the 6 catalog-missed CVEs from n=200 v4

**Date:** 2026-05-27

The n=200 v4 measurement reported 6 CVEs in the
"cleanly-testable but catalog-missed" bucket — CBMC said
the contract holds when the file was in vuln state.  This
note triages each.

## Bug-class summary

| CVE | File:Function | Bug class | Why catalog missed |
|---|---|---|---|
| CVE-2023-54214 | net/bluetooth/l2cap_core.c:l2cap_chan_send | use-after-free | Not a clean kfree-then-deref-in-one-function pattern; UAF crosses function boundaries |
| CVE-2024-35829 | drivers/gpu/drm/lima/lima_gem.c:lima_heap_alloc | resource-leak-on-error-path | Module wired but cocci CFG analysis aborts on lima_heap_alloc (many returns) |
| CVE-2024-43818 | sound/soc/amd/acp-es8336.c:st_es8336_late_probe | resource-leak-on-error-path | Same as above — many returns defeat cocci |
| CVE-2025-21838 | drivers/usb/gadget/udc/core.c:usb_del_gadget | cancel-work-before-free | `cancel_work_before_free` module exists but isn't per-file capable; bug is "missing flush_workqueue before workqueue free" — needs flush_work_before_free shape |
| CVE-2025-68357 | fs/iomap/direct-io.c:__iomap_dio_rw | resource-leak-on-error-path | Same as 35829/43818 |
| CVE-2026-43317 | drivers/most/core.c:most_register_interface | resource-leak-on-error-path | `device_lifetime` module was picked, not `resource_leak_on_error_path` — wrong-module-fit; the underlying cocci would hit the same many-returns issue anyway |

## Bottom line

**4 of 6 are the same shape (resource_leak_on_error_path)
blocked by the same cocci CFG limit**, not 6 different
gaps.  The `resource_leak_on_error_path` module is wired
into per-file synthesis, but cocci's
"inconsistent control-flow paths" guard refuses to insert
the post-alloc `leak_alloc_track` and pre-return
`__assert_no_leak_at_exit` markers when the function has
many return statements that the per-API rule's CFG can't
disambiguate.

## Why ovl_connect_layer detected but lima_heap_alloc didn't

`ovl_connect_layer` (CVE-2025-21654, DETECTED) and
`lima_heap_alloc` (CVE-2024-35829, MISSED) are
superficially the same bug shape.  The difference is
control-flow complexity:

- `ovl_connect_layer`: ~40 lines, single allocation,
  one error-path goto, two normal returns.  Cocci handles
  it cleanly.
- `lima_heap_alloc`: ~50 lines, multiple allocations,
  ~7 returns reachable from the alloc site, nested loops.
  Cocci aborts with "node 67: return ... reachable by
  inconsistent control-flow paths" and instruments
  nothing.

Same diagnosis applies to the other 3 resource-leak
misses — each function has many error-paths the cocci
CFG can't reconcile.

## Why most_register_interface picks the wrong module

The validator's `_MODULE_API_PATTERNS` matches the
function body for kernel-API patterns and ranks modules.
For `most_register_interface`, the function uses
`put_device(iface->dev)` (matching `device_lifetime`) but
ALSO has alloc patterns matching `resource_leak_on_error_path`.
With `--modules-per-cve 3`, both should be tried — but
when `device_lifetime` happens to be picked first AND
yields a clean "successful" verdict, the validator caches
the result and doesn't re-scan with
`resource_leak_on_error_path`.

Fix: either ensure all top-N modules are scanned, or
prefer modules that match the BUG shape rather than the
API-USE shape.

## What would help

For (a)-(c) — the 4 resource-leak misses:

1. **Smarter cocci rule scoping** — narrow each rule to
   single-allocation single-error-path patterns, accepting
   that some will fall through.  Ideally cocci should
   skip the function rather than abandon the file.
2. **Per-function fall-back: explicit `assertion-at-each-
   return` instrumentation** — bypass cocci entirely for
   functions where it gives up.  Insert `__assert_no_leak_at_exit`
   manually before each `return` in the function body.
   This would lose precision (false positives on returns
   that don't actually reach an alloc) but would unblock
   detection.
3. **Different bug-shape model** — instead of
   "alloc-then-no-free-at-return", use
   "exit-state-tracker": at each function exit, assert
   that all tracked allocations are accounted for.  This
   handles the many-returns case naturally.

For (d) — CVE-2025-21838 cancel-work — the existing
`cancel_work_before_free` module needs per-file synthesis
support (similar to what we did for
`resource_leak_on_error_path`) plus a corresponding cocci
rule.

For (e) — most_register_interface — the wrong-module-fit
issue is a validator bug; the correct module exists
elsewhere.

For (f) — l2cap UAF — needs cross-function tracking,
which is outside the per-function scope of our current
methodology.

## Reproducing

The 6 missed CVEs are stable across n=200 runs.  To
reproduce:

```sh
ulimit -v unlimited
python3 integration/linux/doc/scripts/cve_validate.py \
  --n 200 --timeout 600 --invert --modules-per-cve 3 \
  --upstream-repo /home/ubuntu/torvalds-linux.git \
  --out-csv /tmp/cve-validate/results.csv
grep "successful.*MISSED" /tmp/cve-validate/results.csv
```

## Cross-references

- [cocci-cbmc-compile-2026-05.md](cocci-cbmc-compile-2026-05.md) —
  prior closeout that surfaced this list.
- [cve-recall-n200-closeout-2026-05.md](cve-recall-n200-closeout-2026-05.md) —
  earlier closeout with the n=200 baseline.
