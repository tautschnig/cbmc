# Per-file instrumentation tools

Cocci-driven instrumentation that auto-inserts the
synthetic-checkpoint property modules into kernel source
so per-file CBMC verification can catch the corresponding
bug shapes at corpus scale.

## `instrument_null_after_alloc.py` (prototype)

Takes a kernel `.c` source file, finds patterns like
`p = kmalloc(...); ... p->X` without an intervening NULL
check, and rewrites the source to insert
`__assert_safe_to_deref(p);` before each suspicious
dereference.

```sh
./instrument_null_after_alloc.py kernel.c -o kernel_inst.c --header

# Then compile + verify as usual:
build/bin/goto-cc \
  integration/linux/properties/null_after_alloc/null_after_alloc.c \
  integration/linux/scan/adapters/null_after_alloc_kernel_adapter.c \
  kernel_inst.c -o kernel_inst.gb
build/bin/goto-instrument --replace-call-with-contract \
  __assert_safe_to_deref kernel_inst.gb kernel_inst.inst
build/bin/cbmc kernel_inst.inst
```

## Status

This is a **prototype** — it demonstrates the approach
with a single bug shape (kmalloc + immediate deref).
Productising would require:

* Generalising to all synthetic-checkpoint modules
  (resource_leak_on_error_path, integer_overflow_in_alloc_size,
  copy_from_user_size_check, use_after_free_generic,
  division_by_zero_check, uninit_to_user, permission_bypass,
  format_string, the four cancel-before-free modules).
* Replacing the regex with Coccinelle's structural
  matching (the regex misses compound LHS like
  `mk->mp = kmalloc(...)`).
* Wiring into `scan-per-file.sh` as an instrumentation
  step before compilation.
