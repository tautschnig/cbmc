# CodeQL extractor coverage gap on the kernel (and the fix)

**Date:** 2026-06-03
**Trigger:** while resolving `hmm_store` reachability, the CodeQL
path query returned 0 — which prompted the question "is this one
file, or a wider issue?"  It is wider, and the root cause is now
pinned.

## Symptom

In the allmodconfig `drivers/staging` CodeQL DB, many translation
units have **no extracted function bodies** even though the
compiler ran on them.  Measured on the processed source set:

* staging `.c` files the extractor was invoked on: **470**
* of those, with extracted function bodies: **329**
* **compiled-and-invoked but NO bodies: 141 (30%)**

Clustered in the gnarliest drivers:

| subsystem | failed TUs |
|-----------|-----------|
| rtl8723bs | 57 |
| rtl8192e  | 27 |
| media (incl. atomisp) | 21 |
| vt6656 / vt6655 | 22 |
| octeon | 8 |
| gdm724x | 3 |
| vme_user | 2 (incl. vme_user.c itself!) |
| most | 1 |

## What it is NOT

* **Not a make-cache gap.**  First hypothesis was that prebuilt
  `.o`s were skipped by `make` so CodeQL never saw them.  Disproved:
  a full clean rebuild (`find drivers/staging -name '*.o' -delete`
  then re-create) produced the **identical** 329-body / 27-candidate
  result.
* **Not a kconfig gap.**  The files are compiled (`CC [M]` lines
  present); their `.o`s exist.

## Root cause (confirmed from extractor logs)

CodeQL's C/C++ extractor uses an EDG-based frontend.  On the
failing TUs it errors out parsing `arch/x86/include/asm/current.h`:

```
Warning[extractor-c++]: "current.h", line 41: error: expected a ")"
Warning[extractor-c++]: "current.h", line 47: error: incomplete type
                        "struct task_struct" is not allowed
```

Line 41 is:
```c
DECLARE_PER_CPU_ALIGNED(const struct pcpu_hot __percpu_seg_override,
                        const_pcpu_hot);
```
`__percpu_seg_override` expands (under `CONFIG_CC_HAS_NAMED_AS` +
`CONFIG_USE_X86_SEG_SUPPORT`, x86_64) to **`__seg_gs`** — a GCC
*named-address-space* qualifier (`arch/x86/include/asm/percpu.h`).
The EDG frontend does not implement `__seg_gs`, so the declaration
fails to parse, `struct task_struct` is left incomplete, and the
whole TU's body extraction is poisoned.

### Why it is config-sensitive (and why vme_user looked fine before)

`CONFIG_CC_HAS_NAMED_AS` is auto-selected from compiler capability,
and is **on** under allmodconfig with this GCC.  The earlier
dedicated `vme-db` (narrower config) did **not** trigger the
named-address-space path, so `vme_user.c` extracted cleanly there —
which is why the vme_user OOB was found and the staging sweep never
listed vme_user.  The *same file* fails under allmodconfig.  So
extraction coverage is a function of kernel config, not just source.

## The fix

`CONFIG_CC_HAS_NAMED_AS` cannot be turned off via `./scripts/config`
(auto-selected), and `current.h:41` uses `__percpu_seg_override`
unconditionally.  The surgical lever is the single gate in
`arch/x86/include/asm/percpu.h`:

```c
-#ifdef CONFIG_CC_HAS_NAMED_AS
+#if 0 /* CodeQL: force legacy %gs: per-cpu, avoid __seg_gs */
```

This forces the legacy `%gs:`-asm per-cpu path: `__percpu_seg_override`
becomes empty, `__seg_gs` never appears, EDG parses `current.h`, and
GCC still compiles (so CodeQL still traces the build).  It is a
**diagnostic instrument for DB construction only** — reverted after,
never a kernel change.

## Impact on prior results (honest)

* **vme_user OOB — UNAFFECTED.**  Found via `vme-db` where the file
  extracted, and independently confirmed by CBMC + KASAN.
* **Scale-out "27 candidates" — a LOWER BOUND.**  It covered ~70% of
  staging TUs; the 141 unextracted TUs (rtl8723bs, atomisp, vt665x,
  octeon …) were invisible to the taint sweep, so real candidates
  there were missed.  Corrected numbers from the `-noseg` rebuild are
  recorded alongside this file once available.
* **hmm_store reachability — UNAFFECTED conclusion, corrected cause.**
  The `hmm-store-reachability` doc's original "atomisp_cmd.c not
  extracted into the DB" line attributed the gap to a cache/no-clean
  issue; the true cause is this EDG `__seg_gs` parse failure.  The
  manual call-graph trace stands regardless.

## Lesson for the pipeline

Kernel CodeQL DBs must be built with the named-address-space path
disabled (the `percpu.h` lever above) — otherwise a config-dependent
~30% of TUs silently extract no bodies, and a "File is present"
check (declarations leak through headers) masks it.  Coverage must
be measured by *function bodies per source file*, not File entities.
