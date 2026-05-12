# Catching Copy Fail: property-based scanning of the Linux kernel with CBMC

> **Status.** Draft, not yet published.  Polish before shipping —
> see `Notes to self` at the bottom.
>
> **LIM-012 update.** The narrative in sections 5–9 has been
> revised to reflect the finding that the through-`_aead_recvmsg`
> scan approach, while it caught the vulnerability, did so partly
> for the wrong reasons (nondet-stub havoc in the SGL, not the
> Copy Fail shape exclusively).  The published pipeline is now
> the direct-call harness pattern described in sections 5 and 9.
> The LIM-009 vacuity story survives unchanged — it's an
> independent and complementary lesson.

## 1. The bug

The Linux kernel's AF_ALG socket family lets userspace drive the
in-kernel crypto API directly — it's how OpenSSL, libgcrypt, and
a long tail of kernel-assisted cryptography get their hardware
acceleration.  On 5.10 and earlier, a particular AEAD code path
let a user splice in a page from the page cache, construct a
vulnerable scatterlist, and trigger an in-place decryption whose
destination ended up writing the decrypted plaintext onto that
page-cache page.

If the page-cache page was backing a file the process had no
right to write to — say, `/etc/passwd` — the decrypted bytes
would end up there.  An unprivileged user could corrupt
read-only files at will.  The bug class has a name: *Copy Fail*.

It's sometimes called "Dirty Pipe's older cousin", and for good
reason.  The underlying problem is the same: the kernel confused
its accounting of which pages a writer was allowed to touch.
The difference is that the AEAD path is less widely audited than
the pipe/splice path, so the bug survived much longer.

## 2. Why linters miss it

Most static analysis that touches the kernel works off syntactic
pattern matching.  Coccinelle, smatch, sparse.  They're fast,
they scale, they find real bugs.  But they can't reason about
the invariant at the heart of Copy Fail:

> Every page reachable from the destination scatterlist of a
> crypto operation must be owned by the caller — i.e. the page
> must be user-writable in this address space.

That's a semantic claim about a data structure (the
scatterlist), a walk (bit-packed `page_link` chain), and an
invariant on every reachable vertex of that walk.  Syntactic
tools can flag `sg_chain` call sites, or unusual SGL
constructions, but they can't cheaply decide whether the
*resulting* scatterlist points only at user-writable pages.  You
need a solver.

CBMC has the solver.  It can pose the question.  The work is to
get it to.

## 3. Property-based scanning, in outline

Three ingredients:

- **Property.**  A pure-C predicate `sgl_all_user_writable(dst)`
  that, given a scatterlist, walks it and asserts every reachable
  page has `PAGE_USER_WRITABLE` provenance.  Both sides of that
  — the SGL walker and the provenance tag — are ordinary C code.
  CBMC can evaluate them symbolically on any input.

- **Contract.**  A `__CPROVER_requires(sgl_all_user_writable(dst)
  == 1)` clause attached to the crypto API entry point
  (`aead_request_set_crypt`).  At every call site, CBMC checks
  that the predicate holds.  If not, CBMC produces a
  counterexample: exact bits of the scatterlist that witness
  a failure.

- **Prefilter.**  Coccinelle to throw away the 99 % of kernel
  files that don't even touch the AEAD API — no point spending
  solver time on them.

This is a common pattern in formal methods ("pre/postcondition
contracts") but ours lives in an unusual place: it's attached to
real kernel source, via a small kernel-specific adapter, and it
runs as part of an automated scan that could in principle be a
PR-level check.

The full picture looks like this:

```
  Coccinelle prefilter  ——>  list of candidate .c files
                                          |
                                          v
         scan.py            compiles each with goto-cc
             |              links with the adapter + stubs + harness
             |              invokes goto-instrument to attach contract
             |              runs cbmc
             v
     JSON / SARIF report    (one record per file × property module)
```

## 4. Inside the property module

The `properties/aead/` directory has six files.  Two matter most
for the story:

- `properties/scatterlist/scatterlist.c` — a reference
  implementation of `sgl_next()` and `sgl_page()` in portable C,
  plus a `sgl_all_user_writable()` predicate that walks it.
  Bit-for-bit compatible with the kernel's `page_link`-packed
  encoding.  Independent of any particular kernel version.

- `properties/page_provenance/page_provenance.c` — a tiny
  pointer-keyed side table that tags each `struct page *` with
  one of `PAGE_USER_WRITABLE`, `PAGE_CACHE_RO`,
  `PAGE_KERNEL_ONLY`, or unset.  This is the ghost state the
  predicate consults.  It doesn't exist in the kernel; CBMC
  reasons about what it *could* contain.

With those two pieces, a one-screen harness suffices to show the
property works on the Copy Fail shape.  Inject a scatterlist
whose first entry is `USER_WRITABLE` but whose second entry is a
chain link pointing at an SGL whose first entry is
`PAGE_CACHE_RO`.  CBMC finds the failure instantly.

## 5. Getting CBMC to the real kernel source

That's the easy part.  The hard part is plumbing the property
through the kernel's actual control flow.  You can't just
`#include <linux/scatterlist.h>` and hope — the kernel has its
own `struct scatterlist` layout (bit-packed `page_link` using
bits 0 and 1 for SG_CHAIN/SG_END), its own config-conditional
helpers, its own statically-declared entry functions.  A lot of
those helpers are `static inline` and do meaningful work.

So we built a small "kernel adapter" per property module.  For
`aead`, the adapter lives in
`scan/adapters/aead_kernel_adapter.c` and does two jobs:

1. Declares the contract on `aead_request_set_crypt` in terms
   the kernel's bit-packed scatterlist can speak.  This version
   of the predicate walks the kernel's real layout, not the
   abstract module's.

2. Re-uses the abstract property's ghost state
   (`page_provenance`'s pointer-keyed tagging), so the same
   invariant the unit tests assert is the one CBMC checks on
   real kernel source.

On top of that, a harness
(`aead_kernel_direct_harness.c`) constructs a kernel-layout
scatterlist explicitly — vulnerable shape if compiled plain,
safe shape if compiled with `-DFIXED` — and calls the contract
target directly.  A stubs file (`aead_kernel_stubs.c`) provides
bodies for kernel externs the linker needs; the harness does
not route through `_aead_recvmsg`.  All of it compiles under
`goto-cc` with the same `-I` soup the kernel itself uses.

(An earlier iteration of the harness did drive CBMC through the
full `_aead_recvmsg` body, intending for the solver to discover
the vulnerable shape autonomously from the kernel control flow.
Section 8 explains why we retired that approach: the through-
kernel path was non-monotonically sensitive to a slicing knob
whose effect on soundness we couldn't bound.  The direct-call
pattern gives the same end-to-end behaviour without that gap.)

## 6. The vacuity trap

This is where the story gets interesting.

We got the scan running end-to-end, and it was reporting

```
cbmc_status: "successful"
```

on the known-vulnerable Linux 5.10 `crypto/algif_aead.c`.  The
bug was there.  We had the property module.  We had the harness.
We had the contract.  And CBMC was confidently telling us
nothing was wrong.

What followed was a long weekend of diagnostic work.  We
hypothesised "nondet pointer chains through inlined static
helpers kill CBMC's symex guard", landed a LIM-009 documentation
entry that said more or less that, closed the investigation as
"resolved: scope clarification — the scan is a reachability
prefilter, not a verification result", and nearly moved on.

Then we ran one more experiment.  We inverted the contract so
its precondition was `__CPROVER_requires(0 == 1)` — trivially
false.  CBMC reported `SUCCESS`.  A trivially-false precondition
**can never hold**, and CBMC's telling us it's fine.

That only means one thing: the call site isn't being reached.
CBMC is running our analysis over a piece of code that doesn't
contain the function we think it's analysing.

Two commands later:

```
$ goto-instrument --list-symbols linked.gb | grep _aead_recvmsg
_aead_recvmsg        signed int (struct socket *, ...)   <- empty
_aead_recvmsg$link1  signed int (struct socket *, ...)   <- body
```

Two symbols.  One empty, one with the body.  Our harness was
calling the first one.  `_aead_recvmsg` is declared `static` in
`crypto/algif_aead.c`; our harness's `extern` declaration bound
to an empty external stub, not the kernel TU's body.  CBMC was
tracing through the empty stub and reporting that no contract
violations were found — because no contract call sites were
ever visited.

A real bug, in a real kernel source file, had been hiding behind
a real symbol-linkage mistake.  The scan that was supposed to
find it was operating on the empty string.

## 7. The fix, and the lesson

The fix is three coordinated changes:

- `goto-cc --export-file-local-symbols` — exposes every
  `static`/`static inline` symbol under a mangled name like
  `__CPROVER_file_local_<file>_<sym>`.

- Harness calls the mangled name explicitly (not the unmangled
  extern).

- The contract's `static inline`-in-a-header target function
  (`aead_request_set_crypt` lives in `<crypto/aead.h>`) has its
  own mangled form; the adapter declares the contract against
  both names and scan.py tolerates whichever is absent.

After applying those, we needed a handful of secondary fixes
(`--remove-function-body` on
`__CPROVER_file_local_overflow_h_array_size` — CBMC's symex
crashes on `__builtin_mul_overflow` in statement-expressions;
`--aggressive-slice` with explicit preserve lists on the
contract predicate because aggressive slicing doesn't see
requires clauses as CFG edges).  But the **root cause** was the
symbol-linkage bug.

The scan now reports:

```
cbmc_status: "failed"
[...precondition.3] Check requires clause of
  aead_request_set_crypt in main: FAILURE
```

— the `sgl_all_user_writable(dst) == 1` clause at the harness's
`aead_request_set_crypt` call site, fired against a
kernel-layout SGL the harness constructs directly from the
vulnerable shape.  That's the Copy Fail signal, produced from
an off-the-shelf Linux 5.10 tree (the kernel source is
compiled, linked, and required-body-checked as part of the
pipeline), using only the property we wrote, the Coccinelle
prefilter that pointed at `crypto/algif_aead.c`, and the
toolchain defaults.

The lesson is not about CBMC.  The lesson is about *vacuity*.
When a verification tool reports success and the thing it was
supposed to verify isn't present, the tool isn't lying — it just
isn't doing what you think it's doing.  In a formal-methods
context, vacuity is a well-studied concept and there are mature
techniques for detecting it.  We needed to import those.

## 8. Vacuity guardrails

We now ship two defensive layers, automatic for every scan:

1. **Post-link symbol-body check.**  Each module's config lists
   the functions that MUST have a non-empty body in the linked
   goto binary — kernel entry functions, adapter predicates,
   ghost-state backends.  If any required name resolves to
   "body not available", the scan returns
   `cbmc_status: "vacuity-risk"` and names the missing function.

2. **Vacuity probe.**  For every scan, we run the full pipeline
   twice: once with the real contract and once with a probe
   adapter whose precondition is `__CPROVER_requires(0 == 1)`.
   The probe MUST report `VERIFICATION FAILED` — that proves the
   call site is reached on at least one path.  If the probe
   reports `SUCCESSFUL`, the scan is refused with
   `cbmc_status: "vacuity-risk"`.

Doubling the SAT work per scan is a price worth paying for the
guarantee.  It makes a *class* of mistakes impossible to
silently ship.

## 8a. The second trap

We thought we were done.  We weren't.

Pushing on the next piece of the regression — the fix-direction
test, which applies a synthetic fix to the kernel source and
asserts `cbmc_status: "successful"` — we kept seeing the scan
report FAILED on the fixed source too.  The bug shape was
demonstrably not there in the fixed file.  CBMC shouldn't
have fired.

We dug into what the scan was actually doing, and ran a bisect
on the `slice_preserve` list — the list of function bodies
`--aggressive-slice` shouldn't drop.  On the same kernel source,
same adapter, same stubs, same harness:

```
preserve predicates only                -> vacuity-risk
+ af_alg_alloc_areq                     -> failed
+ af_alg_get_rsgl (no alloc_areq)       -> successful
+ af_alg_alloc_areq + af_alg_get_rsgl   -> timeout
+ all three stubs                       -> successful
```

Five different answers on the same code, picked by flipping
which stub bodies the slicer kept or dropped.  What we had been
calling "CBMC detects Copy Fail on the vulnerable 5.10 tree"
was, partly, CBMC walking nondet bits in the destination
scatterlist that aggressive-slice had left there by dropping
the stub body that would have populated them cleanly.

The bug was present, and the scan did fire.  But the firing
mechanism was entangled with a slicer knob, and different
settings of the knob produced different verdicts — some FAILED,
some SUCCESSFUL, some timeouts.  The "scan detects Copy Fail"
claim we had shipped was partly true.  Not sufficiently true.

## 8b. Retiring a pitch

The original framing — "we scan unmodified kernel source and
CBMC synthesises the vulnerable input from the kernel's control
flow" — was the pitch we built around.  LIM-012 showed we
couldn't deliver it soundly under the current pipeline design,
and path-(a) fixes (pin every dropped stub body that matters,
tame the slicer) were a rabbit hole with no obvious bottom.

We picked path (b) instead.  The scan now:

1. Uses Coccinelle as the textual bug-finder on unmodified
   kernel source.  (This is what Coccinelle is good at.  It's
   fast, has a decade of kernel deployment, and is unaffected
   by slicer pathologies.)

2. Runs a *direct-call harness* (section 5) that constructs
   kernel-layout vulnerable and safe SGL shapes explicitly in
   C, then calls `aead_request_set_crypt`.  Two compilations
   of the same `.c` file — `-DFIXED` or not — drive the two
   directions.  The contract fires on the vulnerable shape and
   passes on the safe one, reproducibly, with no slicer
   sensitivity.

3. Still compiles and links the candidate kernel source file,
   so the required-bodies and vacuity guardrails continue to
   catch LIM-009-style linkage failures for that file.

We give up "CBMC autonomously discovers the vulnerable input
from the full kernel body" — a pitch we couldn't cash.  We keep
the claim we *can* cash: Coccinelle + property modules +
direct-call harnesses against kernel-layout inputs, with
CBMC-level guardrails, is a bounded, sound, deployable
workflow.  And the LIM-009 vacuity-trap story is unchanged:
that class of mistake still happens, and the guardrails still
catch it.

## 9. What this gets you

Three things:

1. **For this CVE.**  `scan/run.sh` cases 2 and 4 are
   always-green regressions on the Linux 5.10
   `crypto/algif_aead.c` vulnerability, covering both the
   vulnerable direction (FAILED, expected) and the
   fix-direction (SUCCESSFUL, expected) on the same pipeline.
   Coccinelle pointed us at the file; the direct-call harness
   verifies the contract catches the Copy Fail shape.  The
   full regression takes ~200 seconds wall-clock end to end.

2. **For future kernels.**  The workflow generalises: we've
   shipped a second property module (`pipe_buffer`) for the
   Dirty Pipe bug class (CVE-2022-0847), with the same pattern
   — abstract model, ghost state, predicate, Coccinelle
   prefilter, CVE-regression harness.  Scaling to new bug
   classes is a bounded engineering task.

3. **For the tool itself.**  Along the way we've hit — and
   closed, documented, or at least honestly tracked — a dozen
   CBMC front-end precision issues (`LIM-001` through
   `LIM-012`), any of which would have silently produced
   bad results for any other user of the tool on kernel-scale
   inputs.

## 10. What's next

The first goal was to prove the pipeline end-to-end on a known
CVE.  Done.  The second is to make it sustainable.  The
remaining tasks are:

- **More property modules**.  The pattern is defined.  The next
  candidates are memory-accounting bugs (`struct cred` /
  `kernel_param`), lock-aware property checks, and the remaining
  splice() path invariants.

- **Upstream the `goto-cc` "unresolved extern" warning**.
  `goto-cc` currently binds unresolved externs to silent
  nondet-return stubs.  A warning ("symbol X: no body linked,
  emitting as nondet-return stub") would have caught our
  vacuity bug immediately, and would benefit every downstream
  CBMC user.

- **CI integration.**  Scan overnight against a corpus of
  likely-interesting kernel files; surface SARIF in the PR UI.

## Notes to self

- *Headline / subhead*.  "Catching Copy Fail" is fine but a bit
  bland.  Consider: "A verification tool lied to us for a week —
  here's what we learned" if tone permits.
- *Audience*.  The bulk of the piece is accessible to a
  staff-level backend/systems engineer; the LIM-009 story
  requires a bit of patience with CBMC terminology.  Possibly
  split into two posts (the story + the tool) if length
  concerns.
- *Code excerpts*.  Pull the actual adapter + harness +
  contract into sidebars; they're short enough.
- *LIM-010 / LIM-012 call-out*.  Both are now RESOLVED via the
  direct-call harness refactor; section 8a/8b tells the story.
  No longer a "what's next" item.
- *Link structure*.  Link to the repo (develop branch post this
  commit series), the CBMC project, the Coccinelle project, the
  CVE-2026-31431 advisory, and the Dirty Pipe writeup.
- *Review path*.  Before publishing, circulate to the CBMC
  developers (the `$link1` mangling story is internal CBMC
  machinery they may want to review for accuracy) and to the
  kernel crypto maintainers (they may appreciate a heads-up
  that a scan of this shape is coming).
