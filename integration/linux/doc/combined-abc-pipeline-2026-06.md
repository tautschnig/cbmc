# Combined A+B+C bug-hunting pipeline

**Date:** 2026-06-02
**Status:** Pipeline assembled and demonstrated; verification
back-end is the existing arithmetic property modules.

The `why-no-new-bugs-retrospective-2026-06.md` proposed three
pivots.  They are **orthogonal axes**, not alternatives, and
compose into a single strategy:

| Axis | Question it answers | Mechanism |
|---|---|---|
| **A** | *when / which scope?* | the diff of a fresh git range — code not yet swept by incumbent tools |
| **B** | *which property?* | arithmetic / bounds shapes CBMC uniquely decides |
| **C** | *where?* | a less-swept subtree (staging, vendor BSP) |

Combined statement: **verify the arithmetic-bounds properties
(B) of the newly-added code (A) in a less-swept part of the
tree (C).**

## Pipeline

```
 git range (+ pathspec)              ── A, C
        │
        ▼
 diff_arith_scan.py                  ── selector (A+B+C)
   • git diff --unified=0  -> added lines only      (A)
   • match arithmetic/bounds shapes                 (B)
   • optional --path subtree                        (C)
        │  (file, line, function, shape)
        ▼
 B property modules + per-file harness    ── verification (B)
   • integer_overflow_in_alloc_size   (count*sizeof, alloc(a*b))
   • copy_from_user_size_check        (copy_from_user/get_user/len)
        │
        ▼
 triage_filter.py  ──►  candidate report
```

A delivers a C-like benefit on its own (a fresh diff is
unswept *relative to the incumbents' last run*); `--path`
amplifies it by also choosing structurally less-scrutinised
code.  B is what makes the combination worthwhile: it points
the tool at the one job a bounded model checker does better
than a pattern-matcher — deciding whether a computed size or
index can overflow / go out of bounds for some input.

## Running it

```bash
# A+B: fresh code across the whole tree
python3 integration/linux/scan/diff_arith_scan.py \
    --git-dir /home/ubuntu/torvalds-linux.git \
    --range v7.0..v7.1-rc5 \
    --out-csv /tmp/cand-all.csv

# A+B+C: restrict to a less-swept subtree
python3 integration/linux/scan/diff_arith_scan.py \
    --git-dir /home/ubuntu/torvalds-linux.git \
    --range v7.0..v7.1-rc5 \
    --path drivers/staging --path drivers/net/wireless \
    --out-csv /tmp/cand-staging.csv
```

Then point the B harness at each emitted `(file, func)`:

```bash
# per emitted candidate (file, func), run the arithmetic
# property module via the existing per-file pipeline
integration/linux/scan/scan-per-file.sh <file> \
    --module integer_overflow_in_alloc_size --func <func>
```

## Demonstration (v7.0..v7.1-rc5)

| Selector | candidates | distinct funcs |
|---|---:|---:|
| A+B (whole tree) | 213 | 104 |
| A+B+C (`drivers/staging`) | 6 | 6 |

The whole 30M-line tree — every line of which the incumbent
tools have already swept — collapses to **104 fresh functions
bearing an arithmetic shape**.  That is a tractable set to run
CBMC against, and (unlike a uniform mainline scan) every one
is *new code* exercising CBMC's *comparative advantage*.

Representative A+B candidates (real `count * sizeof` /
length shapes, not the `sizeof(*p)` deref noise the first
regex caught):

* `arch/s390/net/bpf_jit_comp.c` `__arch_prepare_bpf_trampoline`
  — `cookie_cnt * sizeof(u64)`
* `drivers/accel/amdxdna/aie2_pci.c` `aie2_get_clock_metadata`
  — `sensors_count * sizeof(sensor)` used as a `copy_to_user`
    offset *and* a buffer-size check
* `drivers/bluetooth/btintel_pcie.c` `btintel_pcie_alloc`
  — `sizeof(struct tfd) * BTINTEL_PCIE_TX_DESCS_COUNT`

## Honest caveats

* **The alloc-overflow shape is itself being swept in new
  code.** Kernel 7.x introduced overflow-safe allocator
  wrappers (`kzalloc_obj`, `kzalloc_flex`, `kvzalloc_objs`,
  alongside the older `kmalloc_array`/`kcalloc`).  Much new
  allocation code already uses them, so the durable B targets
  are the **length-bounds** shapes (`copy_from_user`,
  `memcpy(.., len)`, computed indices), not raw alloc
  multiplies.
* **The selector selects; it does not prove.** A candidate is
  a place to *run* CBMC, not a bug.  The yield question — does
  running B on these 104 functions actually surface a real
  overflow/OOB? — is the next experiment, not a claim made
  here.
* **This is the right shape of effort, not a guaranteed
  find.** The combination maximises the *probability* of a
  real find (fresh + CBMC's strength + less-swept) but the
  honest expectation remains: most candidates will be safe,
  and the value may still land on regression-prevention.

## Next step

Run B end-to-end on the v7.0..v7.1-rc5 candidate set (start
with the `copy_from_user` / length-bounds shapes, which are
the more durable targets), triage, and record the yield.  If a
candidate's size/index arithmetic is genuinely unbounded for
some input, that is the kind of fresh, CBMC-found bug the
whole effort has been aiming at.
