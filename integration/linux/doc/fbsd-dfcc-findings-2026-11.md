# FreeBSD ENA under DFCC: findings, fixes and proposals (hand-off)

This picks up from commit `fffd090dac3`, which recorded that DFCC loop
contracts "cannot be applied to FreeBSD kernel code" because the CPROVER
library's `malloc`/`free` collide with the kernel's. That diagnosis was wrong,
and the investigation turned up four CBMC defects (three fixed here) and two
open questions, one about allocator modelling and one about proof cost.

Everything below was reproduced with builds from this branch
(`fbsd-dfcc-findings`) and from unmodified develop at `4932a82176a` /
`fffd090dac3` (no differences in `src/`, `regression/` or `unit/` between
the two).

## Branches

| Branch | Base | Contents |
|---|---|---|
| `fbsd-dfcc-findings` (this one) | `fffd090dac3` | the DFCC, inliner and contracts-file fixes below, this write-up, reproducers |
| `dfcc-variadic-write-set` | `fffd090dac3` | subset of this branch: the first three commits |
| `cut-path-reporting` | `4932a82176a` | earlier, independent work on issue C (see the end) |
| `dfcc-issue-c-analysis` | `4932a82176a` | earlier analysis notes on issue C and the UNKNOWNs at ENA scale |

Commits on this branch, oldest first:

- `d5ce47f63ab` DFCC: pass the write set where the write set parameter is
- `79e4fb92c48` goto_inline: insert a nondet value for an argument of
  mismatched type
- `727451d2f73` linux: correct the diagnosis of why DFCC failed on FreeBSD ENA
  (`proof/fbsd/README-fbsd-build.md`)
- `633eae2b016` Loop contracts file: report clauses that fail to parse instead
  of crashing
- the commit adding this file and the reproducers

None of this is pushed.

## 1. The malloc/free conflict is a warning, not the failure

The messages quoted in `fffd090dac3`

```
file <builtin-library-malloc> line 11: 'malloc'
old definition in module ena, /tmp/freebsd-src/sys/sys/malloc.h line 194
  void * (size_t, struct malloc_type *, signed int)
new definition in module <built-in-library>
  void * (__CPROVER_size_t malloc_size)
```

are printed by plain `cbmc` on the same goto binary too, and both `cbmc` and
`goto-instrument --dfcc main` carry on and exit 0. Linking attaches the
library's `malloc(size)` and `free(ptr)` bodies to the kernel's declarations;
the surplus arguments are ignored. What actually stopped
`goto-instrument --dfcc main --apply-loop-contracts --loop-contracts-file
fbsd_cleanup_loop_contract.json` was an `UNREACHABLE` in
`goto_inline_class.cpp` (`parameter_assignments`), much later in the run, on
the variadic `device_printf` -- see 2 and 3.

The renaming attempts (`-Dfree=fbsd_kfree`, `-Dmalloc=...`) addressed the
warning and were not needed.

## 2. DFCC bound the write set to the wrong argument (fixed, `d5ce47f63ab`)

DFCC adds a `__write_set_to_check` parameter to each instrumented function and
appended the write set argument to each call. That is only right when the call
has exactly as many arguments as the definition has parameters. Two cases on
FreeBSD break it:

- **Variadic callees.** `device_printf(dev, "%d", v)` bound `v` to the write
  set parameter. Results are wrong in either direction depending on the value;
  the regression test `variadic_callee_write_set` showed a false "Check that g
  is assignable: FAILURE" for a `g` that is in the caller's assigns clause.
- **Arity mismatch between declaration and definition.** The kernel's
  `malloc(size, M_DEVBUF, flags)` resolved to the library's `malloc(size)`, so
  the write set bound to `M_DEVBUF`, and every kernel allocation and free failed
  the write set checks ("dynamic allocation is allowed", "Check that ptr is
  freeable"). The cleanup harness never calls `malloc`/`free` (they are removed
  as unused), so this did not affect it, but it affects any FreeBSD proof that
  allocates.

The fix: for a variadic function the write set parameter is added *first*
(`va_start` takes the parameters after the last declared one, so a parameter
appended after them would be read as the first variadic argument; this was
the second bug found while fixing the first). Call sites insert the write set
argument at the index of the write set parameter, which is first for a
variadic callee, after the declared parameters otherwise, or wherever an
already-instrumented callee has it. Surplus arguments follow it, where symex
ignores them or passes them on as variadic arguments. The contract wrapper
passes the write set to the wrapped function the same way.

Tests (all `regression/contracts-dfcc`):

| Test | develop | this branch |
|---|---|---|
| `variadic_callee_write_set` | FAILED (false assigns failure) | OK |
| `variadic_callee_write_set_fail` | OK | OK (violation still reported) |
| `variadic_callee_va_arg` | FAILED (false assigns failure) | OK (`va_arg` still returns the argument) |
| `kernel_malloc_free_signature` | FAILED (allocation/free checks) | OK |
| `kernel_malloc_free_signature_fail` | OK | OK (violation still reported) |

`kernel_malloc_free_signature` is not vacuous: an `assert(0)` added in both
contracted functions fails.

**Remaining limitation (not changed):** a variadic function *with a contract*
is called through a wrapper that only forwards the declared parameters, so its
variadic arguments are lost. That predates this work.

## 3. goto_inline aborted on a mismatched argument (fixed, `79e4fb92c48`)

DFCC's loop-assigns inference (`dfcc_infer_loop_assigns_for_function`, called
from `dfcc_cfg_info.cpp`) inlines the callees of a function with loop
contracts *before* that function's own calls are instrumented. Functions are
instrumented in the order of a `std::set<irep_idt>`, i.e. by when their names
were first interned. So whenever a callee was instrumented first, it was
inlined with its new write set parameter against the old arguments. For a
variadic callee that bound, e.g., a `uint16_t` to a pointer parameter, and the
inliner's `UNREACHABLE` fired. On the FreeBSD harness `device_printf` is
interned early (it is declared in `sys/bus.h`), which is what crashed.

The inliner now treats such an argument like a missing one, which it already
handled: warning plus a non-deterministic value. The inference is best-effort
and discards these warnings. Test: `loop_contracts_variadic_callee_inference`
(aborted on develop, OK here). The test names its variadic function `printf`
only because that name is pre-interned and so forces the crashing order.

**Proposal.** The root cause is that inference reads a partially instrumented
model, and its result depends on name-interning order. Running loop-assigns
inference for all functions before instrumenting any would remove both the
mismatch and the order dependence. Not attempted; it is a larger change to
`dfcct::transform_goto_model`.

## 4. Segfault on a loop contract that does not parse (fixed, `633eae2b016`)

`contracts_wranglert::mangle` wraps the clauses from `--loop-contracts-file`
in a dummy function `_fn`, parses it, ignores the result of `parse()`, and
takes `parse_tree.items.front()`. On a syntax error the tree is empty and
goto-instrument died with SIGSEGV (exit 139) right after printing
`file <predicate> line 1 function _fn: syntax error before ')'`. It now throws
`invalid_input_exceptiont`, reported as a usage error (exit 6) naming the loop
and function. Test: `loop_contracts_file_fail/test_syntax_error` (exit 139 on
develop, exit 6 with the message here).

**Open, not fixed: the dummy function has no typedefs in scope.** The clause
that crashed was the FreeBSD invariant extended with
`virt_addr == (uint8_t *)pool_cdesc` and
`cdesc_entry_size_in_bytes == sizeof(pool_cdesc[0])`. Rewritten as
`(void *)virt_addr == (void *)pool_cdesc` and `== 8` it parsed. A cast to a
typedef name cannot parse, because the dummy function is parsed without the
program's declarations (the parser only knows typedef names it has seen);
whether `sizeof(pool_cdesc[0])` also fails was not isolated. Two options:
make the parser aware of the symbol table's typedefs, or document that loop
contract files may only use built-in type names.

## 5. FreeBSD's malloc(9) really is a different API

It is not asm renaming. `sys/kern/kern_malloc.c` defines
`void *(malloc)(size_t size, struct malloc_type *mtp, int flags)` and
`void free(void *addr, struct malloc_type *mtp)` under exactly those names;
the parentheses only stop the `malloc(size, type, flags)` macro of
`sys/sys/malloc.h` from expanding. The kernel is freestanding, so the libc
name is simply reused (Linux avoids the clash with `kmalloc`).

Plain CBMC accepts it by name, as in 1, and so silently ignores the flags.
Reproducer: `proof/cbmc-limitations/fbsd_malloc_flags.{c,sh}`, which uses the
header's declarations, flag values and macro verbatim.

- **`M_WAITOK` may return NULL.** malloc(9) sleeps until it succeeds, but with
  CBMC's default `--malloc-may-fail` the model returns NULL. Code that
  correctly omits the check, e.g. `ena.c:898`
  (`free_rx_ids = malloc(size, M_DEVBUF, M_WAITOK)`), gets false NULL
  dereference failures. `--no-malloc-may-fail` removes those, but also removes
  the real `M_NOWAIT` failure paths that error-path proofs depend on.
- **`M_ZERO` is lost unless the macro can zero in place.** The macro only
  zeroes itself when both `size` and `flags` are compile-time constants;
  otherwise it passes `M_ZERO` to `malloc`, which the model ignores. With
  `--no-malloc-may-fail`, "M_ZERO, constant flags: zeroed" passes and
  "M_ZERO, variable flags: zeroed" fails. `ena.c:895`
  (`malloc(size, M_DEVBUF, M_WAITOK | M_ZERO)`, variable `size`) is this case,
  so `rx_buffer_info` starts with arbitrary contents instead of zeroes.
- **No model at all** for `malloc_domainset` (behind `ENA_MEM_ALLOC_NODE`),
  `mallocarray`, `contigmalloc`, `zfree`, `reallocf`. They are treated as
  functions without body (nondet return value). Not tested separately.

The cleanup harness allocates nothing, so none of this affects it. It does
affect any FreeBSD attach or setup proof.

**Proposals.**

1. A FreeBSD kernel allocator model for the harnesses, defined with the
   kernel signatures so the library model is not used:

   ```c
   void *malloc(size_t size, struct malloc_type *type, int flags)
   {
     (void)type;
     if((flags & M_NOWAIT) && nondet_bool())
       return NULL;                       /* only M_NOWAIT may fail */
     __CPROVER_assume(size <= __CPROVER_max_malloc_size);
     void *p = __CPROVER_allocate(size, (flags & M_ZERO) != 0);
     /* plus the bookkeeping of the library model: __CPROVER_memory_leak,
        __CPROVER_malloc_is_new_array (see src/ansi-c/library/stdlib.c) */
     return p;
   }
   ```

   with `free(addr, type)` mirroring the library `free` (its preconditions and
   `__CPROVER_deallocate`), and `mallocarray`, `malloc_domainset` defined in
   terms of `malloc`. Caveat: DFCC rewrites its own library's allocations to
   call `malloc`/`free` by name (`dfcc_libraryt::fix_malloc_free_calls`);
   with a kernel-signature model those calls would pass no `flags`, so
   `__CPROVER_is_fresh` etc. need checking against such a model.
2. In CBMC: when a library model is attached to a program symbol whose
   declared type differs (as here), say so in plainer words, e.g. "using the
   C library model of malloc for a function declared as ...; arguments 2-3 are
   ignored", rather than the generic "old definition / new definition" pair,
   which reads like a hard conflict and led to the misdiagnosis in 1.

## 6. Why the ring-size-4 cleanup proof takes so long

It is the SAT solver, not symex. Timestamped run of the instrumented model
(`--unwind 5 --unwinding-assertions --sat-solver cadical --verbosity 8`):

| Phase | Time |
|---|---|
| symex (14,819 steps; 1,253 VCCs, 310 after simplification) | 1.7 s |
| conversion to propositional form | 53 s |
| first SAT call: 31,618,911 variables, 137,841,339 clauses | 442 s |
| each further SAT call | 6-20 s alone, up to 125 s when sharing the machine |

All 32 SAT calls completed before the 1800 s timeout were SATISFIABLE: in
all-properties mode each newly failing property costs another call. A variant
run (see below) finished in 916 s with 54 of 837 properties failed after 28
iterations. With `--stop-on-fail` the first failure, after 509 s of solving,
is F11: `ena_get_tx_req_id` line 222, `tx_buffer_info[*req_id]` with a
device-supplied `req_id` of 10 at ring size 4, since the result of
`validate_tx_req_id` is not acted on before the access.

**Where the clauses come from.** `--write-solver-stats-to` attributes 97 % of
the clauses to six SSA steps (`solver_hardness_summary.py`):

| Source | Clauses | Share |
|---|---|---|
| `ena_eth_com.h:260` (`ena_trc_err`, corrupted descriptor), 2 steps | 53.6 M | 38.9 % |
| `ena_eth_com.h:270` (`ena_trc_err`, invalid req id), 2 steps | 53.5 M | 38.9 % |
| `ena_com.h:1186` (`ena_com_get_cap`), 2 steps | 26.9 M | 19.5 % |
| everything else | 3.7 M | 2.7 % |

The first four are the `dev` argument of `device_printf`, i.e. `dev->dmadev`
inside `ena_trc_err`; the last two read `ena_dev->capabilities`. Both
dereference the pointer returned by ENA's `container_of`:

```c
#define container_of(ptr, type, member) ({ ...
    (type *)((uintptr_t)__p - offsetof(type, member)); })
...
return container_of(io_cq, struct ena_com_dev, io_cq_queues[io_cq->qid]);
```

The offset depends on `io_cq->qid`, which is unconstrained: the harness never
sets `ena_dev.io_cq_queues[0].qid` (it sets `tx_ring.qid`), and the loop
contract's assigns clause `__CPROVER_object_whole(&ena_dev)` havocs it at loop
entry anyway. Because the pointer also goes through `uintptr_t`, the value set
of `dev` includes `pool_cdesc` as well as `ena_dev`. Each field read through
`dev` therefore becomes an 8-byte `byte_extract` at a symbolic offset over all
of `struct ena_com_dev`, which gcc measures at 99,648 bytes (256 SQ and 256 CQ
structs of 192 bytes each, plus admin state). That accounts for ~26.8 M clauses
per read.

**An experiment that did not help, and why.** Setting
`ena_dev.io_cq_queues[0].qid = 0` in the harness and adding it, with the other
CQ fields the harness sets, to the loop invariant left the instance at
31,613,486 variables. DFCC havocs `ena_dev` at loop entry, so every instance of
the loop body sees a havocked `qid`; the invariant restores it only as an
assumption, and assumptions do not become constants during symex.

**Proposals, in order of expected effect (none tried):**

1. Narrow the loop's assigns clause from `__CPROVER_object_whole(&ena_dev)` to
   the fields the loop body writes (the CQ's `head` and `phase`, the SQ's
   `next_to_comp`, and whatever `ena_com_comp_ack`/`ena_com_update_dev_comp_head`
   touch), and set `io_cq_queues[0].qid = 0` in the harness. Then `qid` is a
   constant, `container_of` resolves to `&ena_dev` at a constant offset, and the
   six steps should shrink to field reads. Assigns violations will show which
   fields were missed.
2. Stub `ena_log`/`device_printf` so that `ena_trc_err` does not read
   `dev->dmadev` (logging is not part of the obligations). This removes 78 % of
   the clauses on its own, but not the `ena_com_get_cap` reads.
3. Independently, the invariant does not carry the CQ setup (`q_depth`,
   `cdesc_addr.virt_addr`, `cdesc_entry_size_in_bytes`) through the havoc,
   which probably explains many of the 54 failures, including
   `loop_invariant_step` and `main.assertion` INV_NTC / OWN_NO_DANGLING in the
   variant run. Narrowing the assigns clause (1) addresses this too. The
   failures other than F11 have not been analysed.

Reproduction: `proof/fbsd/run_fbsd_cleanup_perf.sh` (about 15 minutes, up to
52 GB) rebuilds the harness from the objects of `README-fbsd-build.md` (link
recipe: harness object plus `ena.o ena_com.o ena_eth_com.o`, `-DRING_SIZE=4`
with `flags.txt`), applies the loop contract, runs `--stop-on-fail` with
`--write-solver-stats-to`, and prints the summary above.

## 7. Unverified observations

- The loop-contract results are reported for loop `ena_tx_cleanup.0`, while
  `fbsd_cleanup_loop_contract.json` names `loop_id` "1". This may just be
  DFCC's own numbering in messages, but someone should confirm the contract
  is on the intended loop (`--show-loops` on the uninstrumented binary).
- `--report-unreachable-properties` (branch `cut-path-reporting`) reported 15
  properties of the instrumented model as UNREACHABLE; they were not
  identified.

## Testing done

- `regression/cbmc`, `cbmc-library`, `goto-instrument`, `contracts`,
  `contracts-dfcc` and `cbmc-cpp` after `d5ce47f63ab` and `79e4fb92c48`: the
  same failures as unmodified develop (`trace-values/unbounded_array`,
  `array_struct_smt`, `array_struct_smt-02`), plus the six new tests passing.
- After `633eae2b016`: `goto-instrument`, `contracts` and `contracts-dfcc`
  again show only the pre-existing `array_struct_smt` and
  `array_struct_smt-02` failures, with the new test passing.
- With all fixes, `goto-instrument` applies `fbsd_cleanup_loop_contract.json`
  to `ena_tx_cleanup` without error and passes `device_printf` the write set
  first.
- Not done: unit tests for this branch, SMT back ends, JBMC (not built).

## Related earlier work (other branches)

On `cut-path-reporting` (based on `4932a82176a`), addressing issue C of
`dfcc-issues-2026-11.md` (a loop contract reported proved on a cut path):

- `b165698b344` fatal assertions: the reachability search that turns PASS into
  UNKNOWN after a refuted fatal assertion now returns to callers (for unknown
  calling contexts only) and continues past `assume(false)`.
- `0ae5f19be61` the assertion of `assert-false-assume-false` stubs (what DFCC
  generates for functions without body) is fatal, so properties after a call
  to such a stub are UNKNOWN rather than vacuously SUCCESS.
- `76dd7385f20` opt-in `--report-unreachable-properties`, decided during symex
  (unlike the reverted `08ec62caff5`), so that properties only reached on cut
  paths, including after a user-written `__CPROVER_assume(0)`, are reported
  UNREACHABLE. Forcing it on across the regression suites exposed five
  existing tests whose SUCCESS is vacuous (listed in that commit message).

On `dfcc-issue-c-analysis`: the UNKNOWN properties at ENA scale are the
documented fatal-assertion behaviour (PASS after a refuted bounds or pointer
check becomes UNKNOWN), with a reproducer.

Open decision from that work: with the option on, a run whose loop-contract
checks are all UNREACHABLE still ends "VERIFICATION SUCCESSFUL" with only a
warning; whether UNREACHABLE should affect the overall verdict is undecided.
