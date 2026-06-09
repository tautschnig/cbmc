# Real-function CBMC harnessing (#1)

**Date:** 2026-06-09
**Goal:** turn the pipeline's stage-2 from *abstract shape* harnesses
(every candidate of an oracle returns the same FAILED) into a **true
per-candidate verdict** on the actual function logic — CBMC's unique
strength.

## Two routes, and why we took the second (for now)

### Route A — full TU through goto-cc (use goto-cc as the kernel CC)
Compile the real kernel object with `goto-cc` instead of `gcc`, then
verify functions in the resulting goto binary.  This is the most
faithful (verifies the code exactly as built) but runs into the CBMC C
front-end's gaps on real kernel headers — the "be prepared for C
front-end fixes" reality.  Findings:

* **`-Werror` escalation (FIXED, reusable).**  goto-cc emits
  "incompatible pointer types" as a *warning* (e.g. the kernel's
  `LIST_POISON1 = ((void*)0x100 + DELTA)` which GNU-C types as `char*`
  and assigns to `struct list_head *`).  The kernel build passes
  `-Werror`/`-Werror=incompatible-pointer-types`, which goto-cc honors
  and escalates to a hard error.  **Stripping `-Werror*` from the
  goto-cc invocation clears this whole class** and is sound for our
  purpose (the conversions don't affect the values CBMC reasons about).
* **Unmodelled GCC attributes (long tail, OPEN).**  `__nonstring`
  (`__attribute__((__nonstring__))`) and similar trip
  "failed to find symbol 'nonstring'" — the parser doesn't consume the
  attribute and the typechecker then treats the name as an identifier
  expression.  Emptying the macro in `compiler_attributes.h` did not
  fully clear it (more usages/constructs behind it).  A *general* fix is
  a CBMC-front-end change to ignore unknown `__attribute__` names (plus
  `counted_by`, etc.); that is a parser/typecheck patch + CBMC rebuild,
  scoped as future work.  `cprover_kernel_compat.h` is the start of the
  force-include shim for the macro-level neutralisations.

Conclusion: full-TU verification is a multi-construct front-end project
(each gap = a parser/typecheck fix + rebuild).  Worth doing, but not a
blocker for the per-candidate-verdict goal.

### Route B — faithful extraction (DONE, the verdict we wanted)
Copy the **actual function body and its real helpers** into a
self-contained unit (no kernel headers → no front-end minefield), with
kernel types/helpers reproduced at their real byte-level behaviour, and
drive it against a buffer of exactly the attacker-controlled length.

`real_nfc_llcp_parse.c` does this for **`nfc_llcp_parse_gb_tlv`
(CVE-2026-31622)**:

* the verbatim parse loop (`while (offset < tlv_array_len) { type=tlv[0];
  length=tlv[1]; ...; offset += length+2; tlv += length+2; }`, with the
  real `u8 offset` that can wrap);
* the verbatim `llcp_tlv8`/`llcp_tlv16` helpers (which read `tlv[2]` /
  `tlv[2..3]`), gated by the **real** `llcp_tlv_length[]` table
  `{0,1,2,2,1,1,0,1,0,2}`;
* a `malloc(tlv_array_len)` buffer so CBMC bounds-check sees exactly the
  valid bytes.

Result:

```
harness_buggy  (real unguarded loop)        -> VERIFICATION FAILED
   [be16.pointer_dereference] pointer outside object bounds  (the real OOB)
harness_fixed  (CVE-2026-31622 bound added) -> VERIFICATION SUCCESSFUL
```

This is a **true verdict on the real logic**: the unguarded function is
provably OOB for some bounded input, and the fixed version (per-element
`offset + 2 + length <= tlv_array_len` check, plus widening `offset`
beyond `u8`) is provably safe for all bounded inputs.  Note it also
exercised a subtlety the abstract harness misses — the verdict depends
on the real `llcp_tlv_length[]` invariant (`tlv[1] == llcp_tlv_length
[type]` gates the helper read), which a zero-stub got wrong; faithfulness
matters.

## Status / next

* Route B gives the credible per-candidate verdict today and is the
  template to generate for differential-scan candidates (extract the
  touched function + its real helpers, drive with a sized buffer).
* Route A's `-Werror` strip is a keeper; the attribute long-tail is a
  scoped CBMC-front-end task (ignore unknown `__attribute__`s) that, once
  done, would let us verify whole real TUs without extraction.

## UPDATE (2026-06-09): Route A now works — full-TU verdict achieved

Three CBMC C front-end fixes unblocked compiling real kernel objects
with goto-cc (each committed with a regression test):

1. `__builtin_has_attribute` — parse + model as constant `false`
   (commit 6e35acd7c1).  The actual blocker behind the earlier
   "failed to find symbol 'nonstring'": the fortify `strscpy` macro
   expands to `_Static_assert(!(!(!__builtin_has_attribute(dst,
   nonstring))), …)`.  (Unknown *attributes* were already ignored —
   the original assumption was right; this builtin was the gap.)
2. `-fms-extensions` anonymous *tagged* struct/union members
   (commit 718e6b17e9) — `struct __filename_head;` in struct filename;
   added `config.ansi_c.ms_extensions`, gated so non-ms code is
   unchanged.
3. `a ? : b` (gcc_conditional_expression) constant-folding in
   `make_constant` (commit 71658b0ac6) — the kernel's
   `__aligned((x + 0) ? : SMP_CACHE_BYTES)` cache-line macro.

### Recipe (goto-cc as the kernel CC)
```sh
# 1. capture the exact gcc command kbuild uses
CMD=$(make V=1 net/nfc/llcp_commands.o 2>&1 | grep -E '^\s*gcc .*\.c\s*$' | tail -1)
# 2. swap compiler + output, strip -Werror* (goto-cc emits
#    incompatible-pointer-types as a warning; the kernel -Werror would
#    escalate it)
GCMD=$(echo "$CMD" | sed "s#^\s*gcc #goto-cc #; s#-o [^ ]*\.o#-o llcp.gb#; s/-Werror[=a-z-]*//g")
bash -c "$GCMD"            # -> llcp.gb  (full TU, real kernel headers)
```

### Verdict on the REAL compiled function
`llcp_gb_tlv_realtu_harness.c` allocates a `malloc(len)` buffer and calls
the real `nfc_llcp_parse_gb_tlv` from `llcp.gb`:
```sh
goto-cc -o h.gb llcp_gb_tlv_realtu_harness.c
cbmc llcp.gb h.gb --function harness --bounds-check --pointer-check --unwind 6
```
Result: **VERIFICATION FAILED** — CBMC finds the genuine CVE-2026-31622
OOB in the *actual compiled* helpers:
```
[llcp_tlv16.pointer_dereference] line 42 pointer outside object bounds in tlv[0/1] / *((__be16*)(tlv+2)): FAILURE
[llcp_tlv8.pointer_dereference]  line 34/37 pointer outside object bounds in tlv[0/1/2]: FAILURE
```
This is a true per-candidate verdict on the real function — real loop,
real `llcp_tlv8/16` helpers, real `llcp_tlv_length[]` table — not an
extraction or abstraction.  Route A (full TU) and Route B (faithful
extraction) now agree.

### Caveat
Compile the harness with the kernel's `-funsigned-char -fshort-wchar`
to silence benign `__CPROVER_architecture_*` link warnings (they don't
affect the verdict).  Further kernel TUs may surface additional
front-end gaps; the three above cleared net/nfc/llcp_commands.c
end-to-end.
