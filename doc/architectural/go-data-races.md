# Data-race detection for the Go front-end: design

Status: implemented, opt-in (`--go-race-check`). Sections 1 to 7 are the
original design; sections 8 to 12 record the implementation and its
evaluation against Go's runtime/race tests.

## 1. What must be reported

GoMem §Memory_model: "A read-write data race on memory location x
consists of a read-like memory operation r on x and a write-like memory
operation w on x, at least one of which is non-synchronizing, which are
unordered by happens before". Write-write races are defined in the same
way. A memory location is a variable, a struct field or an array element
(GoMem: "a read of an array, struct, or complex number may be
implemented as a read of each individual sub-value"), so two goroutines
that write different elements of one slice do not race.

The happens-before edges are those of GoMem: `go` statements, channel
operations (including "the kth receive from a channel with capacity C is
synchronized before the completion of the k+Cth send"), `sync.Mutex` and
`RWMutex`, `Once`, `sync/atomic` (sequentially consistent), and, from the
package documentation, `WaitGroup` and `Cond`.

The result is a property per access site, of class `data-race`, failing
if a race involving that access is possible within the unwinding bound.
GoMem allows an implementation to react to a race by reporting it and
terminating the program. Otherwise, a racy read of a value of more than
one word may observe a torn value and corrupt memory arbitrarily. gc's
race detector reports and continues by default (`halt_on_error=0`). We
report the race, and properties that may be checked after it are UNKNOWN
(section 11).

## 2. What the front-end already provides

- Goroutines are CBMC threads (`__CPROVER_ASYNC_1`, `convert_go`), and
  goto-symex encodes their interleavings sequentially consistently.
- Every synchronising operation of the models (channels, `select`,
  `sync`, `sync/atomic`) is an atomic section (`rt.AtomicBegin`,
  `rt.AtomicBeginWhen`, `block_until`), and blocking is `assume`. So the
  happens-before edges of §1 hold in every interleaving the encoding
  admits, without being represented explicitly.
- Shared data are mostly heap objects (escaping variables, closure
  environments, slices, maps, channel buffers), symex dynamic objects
  rather than named globals.

## 3. Approach: conflicting accesses that can be adjacent

By the classic result behind DRF-SC (Adve and Hill; Boehm and Adve), a
program has a data race if and only if it has a sequentially consistent
execution in which two conflicting accesses, at least one not
synchronising, are adjacent. Since the encoding already enumerates all
SC interleavings (within the bounds), it suffices to make adjacency
observable. That is the scheme of goto-instrument's `--race-check`
(`src/goto-instrument/race_check.cpp`), per location instead of per
named object:

```
write of x:  W[x] := 1; <the write>; W[x] := 0; assert(!W[x] && !R[x])
read of x:   R[x] := 1; <the read>;  R[x] := 0; assert(!W[x])
```

Another goroutine scheduled between a set and a reset makes the
assertion of a conflicting access fail. One flag per location and kind
suffices even with several readers, as the checker explores the
interleaving in which the racing pair is adjacent. Accesses inside the
models' atomic sections are synchronising: they are not flagged
themselves, but still check the flags (GoMem counts an atomic access
racing with a plain one as a race).

A probe confirms the scheme in CBMC (`~/go-scratch/race-probe`,
`flag_threads.c`): with a shared flag array per element, the racy access
is found and an access to another element is not flagged.

### Rejected alternatives

- **goto-instrument `--race-check`** as is: it excludes heap objects
  (`symex_dynamic::dynamic_object*`, so almost all Go data), names
  objects statically (no array elements: "we don't distinguish the array
  elements for now", `rw_set.cpp`), instruments assignments only (not
  reads in conditions or arguments), and needs points-to sets before
  symex.
- **A happens-before query in goto-symex's partial-order encoding**
  (`partial_order_concurrency`, `memory_model_sc`): precise and without
  instrumentation, but the encoding has no happens-before relation of
  its own (only one total order), nor a notion of synchronising
  accesses; a new memory model with hb clocks and pairwise
  "unordered" queries over events of the same object, with offset
  comparisons for elements, is a much larger change to symex and to
  the formula.

## 4. Where the flags live

The flags must be per byte (or element) of every object, heap objects
included, and visible to all threads.

**Option A (preferred): CBMC shadow memory, made thread-aware.**
`__CPROVER_field_decl_global("W", (_Bool)0)` and `__CPROVER_set_field`
/ `__CPROVER_get_field` give byte-granular fields for globals, locals
and heap objects (initialised at `ID_allocate`), resolved through the
value sets of the access, which the front-end has exactly (the address
of the accessed lvalue). It does not work across threads today: the
probe `sm_threads.c` misses the race, because the shadow symbols
(`SM__…`, `get_fresh_aux_symbol`) are not shared, so their accesses are
not shared events, and the registry of shadowed objects
(`shadow_memory_statet::address_fields`) is per symex state, which each
thread copies at spawn. The fix belongs in goto-symex (and helps C
programs too): shadow symbols shared (static lifetime), and a registry
that objects allocated by any thread join (with the repetition that
multi-path symex already does for the shared value summary). Costs: each
access adds a few shared events and a case split over its value set.

**Option B (fallback, front-end only): a global flag array indexed by
pointer.** `W[POINTER_OBJECT(p) * 2^offset_bits + POINTER_OFFSET(p)]`
in one unbounded array, which the array theory handles. No symex changes,
but every flag access is to the same shared object, so the memory model
relates all of them pairwise: likely quadratic in the number of
instrumented accesses.

Both are behind the same hooks (§5), so the choice can be revisited.

## 5. Instrumentation in the front-end

As gc does for `-race` (calls of `runtime.raceread`/`racewrite`), the
converter inserts hooks `rt.RaceRead(p, n)` / `rt.RaceWrite(p, n)`
(intrinsics, §4) around accesses through addresses:

- loads and stores of package variables, of heap variables (escaping
  locals and captured variables: `state.heap_objects`), and through
  pointers, slices (elements), maps (the map as a whole, as gc's
  "concurrent map read and map write" fatal error), struct fields and
  array elements of those; not locals on the stack or temporaries;
- built-ins as ranges: `copy` (reads of `src[:n]`, writes of `dst[:n]`),
  `append` (writes of the new elements; reads of the copied ones),
  `clear`, `delete`, map assignment and lookup, `len` and `cap` of maps
  and channels;
- the source models of the standard library (plain Go, instrumented as
  gc instruments the standard library under `-race`), but not the code
  inside atomic sections, nor `cprover/rt`, `sync`, `sync/atomic` and
  the channel code, which synchronise (their accesses to user memory,
  e.g. `atomic.StoreInt32(&x, 1)`, only check the flags);
- not reads of objects that are never written after initialisation
  (const descriptors, string bytes, constant tables).

When no goroutine can be started (no reachable `go` statement, including
the timer goroutines of `time`), nothing is instrumented, so sequential
programs pay nothing. `--go-no-race-check` turns the check off, as
`--go-no-deadlock-check` does for deadlocks.

Happens-before edges that the models do not reproduce precisely today
must be fixed with them, since a missing edge is a false race and an
extra edge a missed one: `TryLock` "may be considered to be able to
return false even when the mutex l is unlocked" (the model never fails
spuriously: fine, an extra behaviour is not needed for races); `Once`'s
completion before every return of `Do` (the model's mutex gives it);
`WaitGroup.Done` before the `Wait` it unblocks (the model's
`AtomicBeginWhen(n == 0)` gives it).

## 6. Validation

- `src/runtime/race/testdata` has 372 tests named `TestRace…` and
  `TestNoRace…` (channels, mutexes, `sync`, maps, slices, `select`,
  closures, `atomic`, `rangefunc`), a labelled oracle: each becomes a
  harness; `TestRace…` must fail a `data-race` property and `TestNoRace…`
  must not, within a stated bound. Tests that need unmodelled packages
  (`os`, `runtime` internals, cgo) are excluded and counted.
- Regression tests per edge of §1, in both directions (the race without
  the synchronisation, and none with it), and the C probes for the
  shadow-memory fix in goto-symex.
- Profiling (plan §7.2): the concurrency benchmarks of
  `scripts/profile_cbmc.py --auto-concurrency` with the check on and off.

## 7. Steps

1. goto-symex: thread-aware shadow memory (Option A), with C tests and
   profiling; or decide for Option B if that proves too invasive.
2. Front-end hooks and their implementation; instrumentation of user
   code; the switch and the no-goroutine shortcut.
3. Built-ins and maps as ranges; source models instrumented; atomic
   operations as checking-only.
4. The runtime/race corpus as an oracle; fix model edges where it
   disagrees.

Open question for review: whether a race should also stop the path (as
gc does), which keeps later properties from failing because of torn
reads of multi-word values (interfaces, slices, strings), at the price
of not checking anything after the first race on that path.

## 8. Spike results

The spike is a goto-level pass (`src/go/go_race_check.cpp`, option
`--go-race-check`) rather than front-end hooks: it instruments the goto
program after conversion. Accesses are flagged at the first byte of each
scalar part, which avoids shadow memory's replication over bytes of
pointer type.

**Oracle.** 341 harnesses, one per `TestRace…`/`TestNoRace…` function of
`runtime/race/testdata`, with a stub `testing` package
(`~/go-scratch/race/oracle`: `gen.py`, `run2.sh`). As ground truth,
every harness was also built with `go build -race` and run five times:
gc agrees with the names on 337, and the other 4 fail under the stub
(`t.Skip`, `t.Fatal`). The check uses `--unwind 4`, 8 GiB and 600 s per
harness.

| | found / clean | missed / reported | solver out of memory | timeout |
|---|---|---|---|---|
| 224 `TestRace…` | 171 | 32 | 15 | 6 |
| 117 `TestNoRace…` | 108 | **0** | 0 | 9 |

Before the fixes below: 110 found, 92 missed, 17 crashes, 1 false
report.

**The 32 misses, by cause:**

- 19: range accesses of built-ins (`copy`, `append`, string conversions;
  also in source models such as `bytes.Buffer`). These are not
  instrumented yet (§5).
- 4: races "ordered" only by non-synchronising operations: polling
  `len(c)`, a failed `select` send, `time.Sleep`. These are inherent to
  DRF-SC adjacency. Under SC the accesses are never adjacent, while gc's
  happens-before relation does not order them.
- 3: large arrays (`[1000]*int` keys) are flagged at their first byte
  only, beyond 32 elements.
- 1: `delete` of an absent key is no write (gc's `mapdelete` writes the
  map unconditionally).
- 1: operations of the `sync` model are not accesses (`mu = Mutex{}`
  racing with `mu.Lock()`).
- 3: harnesses that are not valid under the stub (see above), and 1 that
  has not been analysed.

**Bugs the spike found outside the check itself:**

- goto-symex (`8d3b69c931`): renaming produced conditionals and byte
  updates of inconsistent types after a phi node of an array size symbol
  (invariant failure).
- Shadow memory (`97f789ce71`): objects of pointer type were not
  accessible through `char *`, so the flags of every captured pointer,
  channel and map were silently dropped.
- Shadow memory is not thread-aware (§4). Making the shadow symbols
  static-lifetime and not thread-local suffices for the oracle.

**Semantics the oracle settled:**

- `_ = x` reads `x`.
- `close` is a write of the channel and a send a read; the fields of the
  channel's header are not accesses.
- Map operations are accesses of the map's header.
- Atomic operations announce, but do not check: they race with plain
  accesses only.
- Setting or resetting a flag must be atomic. It is a read-modify-write
  of the shadow object, and a lost update caused false races.
- Checks must take the place of the instruction, so that jumps to it are
  checked.

**Flag storage, measured:**

| program | no check | option A (shadow memory) | option B (flag arrays) |
|---|---|---|---|
| `go_pipeline` (profiling benchmark): steps | 58,289 | 65,440 | 203,641 |
| `go_pipeline`: decision procedure | 34 s | 55–62 s | 551 s |
| map read/write in two goroutines: decision procedure | 10 s | out of memory (also at 24 GiB) | 13 s |
| `r1` probe | | 0.6 s | 359 s |

Option A is about 10× faster than option B on the probes and the
benchmark. It fails on programs that grow maps, however: the 15
solver-memory errors are map and `append` harnesses. The memory goes
into array-theory constraints (`arrayst::add_array_constraints_comprehension`,
64-bit `mod` dividers in byte-operator lowering), and 12,000 of the
16,000 read-from constraints are on the shadow object of the map header.
I have not isolated the cause further.

**Recommendation.** Keep option A, with three pieces of further work:

1. Instrument built-ins as ranges (19 misses). Shadow memory needs a
   range form for this (set/get over `n` bytes of a variable-size
   object), or the check needs bounded loops.
2. Investigate the shadow-memory blow-up on growing maps before
   upstreaming the thread-aware shadow memory.
3. Treat `sync` operations as accesses of the lock (gc does).

The 4 inherent misses are a documented limitation of the adjacency
approach. Detecting them would need a happens-before relation that
excludes non-synchronising communication, which this approach was
chosen to avoid (§3).

Open questions for review, unchanged: option A or B, and whether a race
ends the path.

## 9. No silent misses

Review of the spike (section 8): the check must find every potential
race within the bounds, or fail a property that says it cannot. Like
CBMC's unwinding assertions, it must not quietly under-approximate. Each
way the spike could miss a race is now closed, or turned into a failing
property.

| Gap | Now |
|---|---|
| Shadow memory cannot resolve an object (unknown value set, no shadow memory) and drops the flag with a warning | `__CPROVER_shadow_memory_incomplete` is set (goto-symex, `6f0e9ce28c`), and every instrumented access asserts it is false: "data race check: shadow memory does not resolve the accessed object" |
| An object allocated by a thread that symex runs later had no shadow memory in earlier threads | registrations of all threads and paths, and of the previous symex run, are consulted (`99d23955dd`) |
| Bytes of pointer-typed objects not accessible through `char *` or `char[N] *` | fixed in shadow memory (`97f789ce71`, `7cdcc55c4e`) |
| A goroutine started in the last unwound iteration read unconstrained arguments (spurious failures) | symex saves and restores a thread's names at its start (`6b95e60032`) |
| Only the first byte of an access was flagged | all bytes are flagged (`unsigned char (*)[N]`), so overlapping accesses conflict wherever they start, also through `unsafe.Pointer` |
| Arrays over 32 elements, and parts beyond the budget, were flagged at their first byte | parts over 64 bytes, and bulk operations (`array_copy`, `array_replace`, `array_set`, `havoc_object`: `copy`, `append`), are accesses of the whole object (RW/RR flags), which conflict with any access of the object (OW/OR) |
| `cprover/rt` helpers and generated `go::$` functions were not instrumented | they are; `sync` and `sync/atomic` are models: their atomic sections are announced without checks, and outside them only `rt.RaceRead`/`RaceWrite` count (gc's annotations, such as `WaitGroup`'s `sema`) |
| Other accesses in atomic sections (model state) were not announced | announced, without checks |
| Locals whose address is taken | shared; shadow memory has no local fields, so these fail the incompleteness check |
| `delete` of an absent key was no write | a write of the map, as gc's `mapdelete` |
| Observations that do not synchronise (`len(c)`, a `select`'s default case, a failing `TryLock`) order accesses in every SC interleaving without a happens-before edge, so the races they order were missed (`TestRaceChanSendLen`, `RecvLen`, `SendSend`) | "data race check incomplete: an observation that does not synchronise ... may be concurrent with the change it observes": a check that no change of the observed state (announced after each channel section, and by the sync models' sections) is concurrent with it |
| Unwinding bounds, unsupported constructs, solver limits | loud as before: unwinding assertions, `unsupported` properties, and errors or timeouts |

**Remaining assumptions, which I believe are sound:**

- Synchronisation lives only in the models' atomic sections (channels,
  `sync`, `sync/atomic`).
- An observation that only shows another operation has not happened yet
  (an atomic load of an initial value, a `select` that finds a case
  ready) creates no false ordering under adjacency. Only observations
  that a change has happened do, and these are the ones marked.
- The model of `time` lets the clock jump forward arbitrarily, so reading
  it never forces an order between goroutines.
- Accesses in a model's atomic section are announced at the section's
  start, with addresses evaluated there; the models do not assign the
  pointers they access within a section.
- Accesses of whole objects are coarse: `copy(a[:5], b)` is reported as
  racing with `a[7] = 1` (`TestNoRaceSliceWriteCopy`).
- The observation check also fails where gc reports nothing
  (`TestNoRaceChanCloseLen`, `TestNoRaceCloseHappensBeforeRead`). These
  are inconclusive results, not race reports.

## 10. Solver out-of-memory failures

Run I (section 9's fixes) had 26 harnesses that failed for lack of solver
memory or crashed, and 28 timeouts. Most of the causes are not specific to
the race check; they also affect concurrent Go programs that grow slices or
maps.

| Cause | Fix |
|---|---|
| Byte operators between arrays of the same element type, of symbolic size, lowered through bytes: a comprehension with divisions by the element size, instantiated for every index (a two-goroutine `append` beyond capacity: out of memory at 24 GiB) | lowered element by element (`357c121d83`); also when only one size is symbolic (`ec006be75e`: ten `struct{x, y int}` copied between goroutines went from 10 million variables and 168 s to 73,000 and 0.4 s). Both also fix spurious failures (`array_copy_symbolic_size`, `array_replace_constant_from_symbolic`) |
| `simplify_byte_update` built one `with` per element of the value, 8,000 deep for a `[1000]*int` map key into an empty map's array, which overflowed the stack | empty arrays are not updated, and chains of more than 256 elements are left to lowering (`8c5154f7ef`) |
| Shadow memory of a dynamic array typed with its size symbol, mismatched when reused from the previous symex run | unbounded shadow arrays (`ac23ea0997`) |
| The race checks read the flags of every byte of an access (82,113 instead of 36,441 steps) | the first byte suffices for the check (`969b834200`): of two overlapping accesses, the check of the one starting later can be scheduled to see the other's flags |

**Run J on the whole oracle, `--unwind 4`:**

| | found / clean | inconclusive (incomplete, unwind) | error | timeout | silent / false |
|---|---|---|---|---|---|
| 224 racy tests | 203 (run I: 177) | 7 | 3 (run I: 24) | 8 (run I: 14) | 3* |
| 117 race-free tests | 79 | 26 | 1 | 10 | 1** |

\* `TestRaceIssue51618`, `TestRaceIssue5567` and `TestRaceRangeFuncIterator`
are not valid under the stub `testing` package (gc fails them too).

\*\* `TestNoRaceSliceWriteCopy`, from the coarse whole-object accesses.

The oracle took 37,535 s in total (run I: 50,125 s).

**Remaining errors:**

- Very large values: `[1000]*int` keys and values copied into maps
  (`TestRaceMapBigKeyInsert`, `BigValInsert`; the array theory compares
  64,000-bit elements). `TestRaceNestedArrayCopy` copies a 16 MB array
  and fails without the race check too.
- 2,047 goroutines (`TestNoRaceDeferAtomicStore`): the memory model's
  constraints.

## 11. After a race

The Go specification says nothing about races; the memory model (GoMem)
does. An implementation may report a race and halt. If it does not, a read
of a word-sized or smaller location observes some write that precedes it
or is concurrent with it. A read of a larger value may observe a torn
value, which can lead to arbitrary memory corruption. Our encoding never
tears values. A property proven after a race would then only hold if
nothing was corrupted, which is a quiet under-approximation.

The data-race assertions are therefore *fatal* properties, as CBMC uses
for undefined behaviour in C (`8261ea72ce`). Symex continues past them,
and after the solver has run, proven properties that may be checked after
a refuted fatal property are reported as UNKNOWN. Ending the path at a
race (an assumption after each check) was the alternative. It would
prune the state space, but properties after the race would still say
SUCCESS, proven on race-free paths only.

The computation of what follows a refuted fatal property
(`goto-checker/fatal_assertions.cpp`) missed code in two cases, and both
are fixed in general, also for C (`627d2df578`):

- the code after the call sites of the function holding the property,
  once it returns; and
- the code of other threads, which does not follow the property in the
  control-flow graph: the code of the main goroutine after a `go`
  statement, or a goroutine started earlier. When a fatal property is
  refuted, the computation now also starts from every `START_THREAD`
  instruction. When code runs concurrently with or after the race, at
  least two threads exist, and all such code is reachable from some
  `START_THREAD`. Only code that runs before the first goroutine starts
  keeps its status.

The properties that say the check is incomplete ("data race check
incomplete: ...", "shadow memory does not resolve the accessed object")
are not fatal, as they do not say a race happened. A racy harness now
reports the race and UNKNOWN for nearly every other property;
`regression/go/race-fatal1` shows this.

## 12. Unwinding, precision, and run K4

**Inconclusive results from unwinding.** Run J had 26 harnesses that
failed an unwinding assertion. Grouped by the loop that failed:

| Loop | Harnesses | Now |
|---|---|---|
| Spin-waits, such as `for atomic.LoadInt32(&s) != 1 { runtime.Gosched() }` | 9 | an await: at most one iteration that continues the loop, then an assumption that the condition fails (`41517a9373`). An iteration that continues writes nothing other goroutines can observe (the condition is read-only, or a failing compare-and-swap; the body calls only `runtime.Gosched` and `time.Sleep`), so removing such iterations keeps adjacent accesses adjacent; the one kept iteration covers the checks of any of them |
| `for range s {}` | 3 | no loop (`47115f657c`) |
| `copy` and `append` of elements with pointers, one by one | 5 | by `__CPROVER_array_copy` (`5b1d9668b5`), which keeps pointers since the element-wise lowering of section 10 |
| comparisons with `"sync: negative WaitGroup counter"` (`rt.StringEqual`) | 2 | comparisons with constants of at most 64 bytes, also through interfaces, compare the length and each byte (`ad859c7571`). Comparing arbitrary strings as arrays of a common symbolic length (`__CPROVER_array_equal`, with a witness of a difference) needs no loop either, but made encoding/json from source 40 times slower |
| loops of fixed counts above the bound (4 to 1,000,000 iterations, 32 goroutines) | 7 | unchanged: they need a larger bound |

**Precision.**

- `copy` accesses the elements it copies, not the whole objects
  (`1da8788be7`): range markers `go::$race_read_range` and
  `go::$race_write_range`, flagged as an array of unsigned chars of
  symbolic size. Copies of map entries keep no markers (`2c978538d3`),
  as their accesses are of the map's header. This needed two fixes in
  CBMC itself: a byte update of symbolic size into an array of elements
  wider than a byte left its last element unchanged (`5a16a5847d`, also
  in the 6.x releases), and symex failed on a set_field of symbolic size
  whose value depends on the path (`731c8613f7`).
- A failed symbol in a value set makes shadow memory incomplete only if
  the pointer points to it (`bb98255232`): value sets merge paths, so a
  pointer that is assigned wherever it is used still has one.
- Objects that a model's atomic section allocates (the value of a
  run-time error, with `recover`) are not shared before the section ends,
  and are not announced (`49146dfe50`): their pointers are not assigned
  where the section's accesses are announced.

**Run K4 on the whole oracle, `--unwind 4`** (build `036b30283a`, the
map harnesses rerun with `2c978538d3`):

| | found / clean | incomplete | unwind | error | timeout | silent / false |
|---|---|---|---|---|---|---|
| 224 racy tests | 208 (run J: 203) | 3 | 1 | 3 | 6 | 3* |
| 117 race-free tests | 99 (run J: 79) | 2 | 5 | 1 | 10 | 0 (run J: 1) |

\* As in run J, invalid under the stub `testing` package.

The oracle took 37,580 s in total (run J: 37,535 s).

**Remaining:**

- Observations that do not synchronise (`TestRaceChanSendLen`,
  `RecvLen`, `SendSend`, `TestNoRaceChanCloseLen`): the check reports
  them as incomplete. Letting such an observation (len of a channel, a
  select's default case, a failing TryLock) return any value it could
  return in some interleaving would remove the order it imposes, and so
  find these races, but also admits behaviours the program does not have
  for its other properties.
- `TestNoRaceProducerConsumerUnbuffered`: values that cross an
  unbuffered channel have pointers whose value sets are unknown (it timed
  out in run J).
- Timeouts: symex itself is slow on goroutines that loop over `select`
  (`TestNoRaceSelect4`: 187 s of symex without the check, more than 600 s
  with it).
- The errors of section 10 (very large values, 2,047 goroutines).


## 13. Symex time and very large values

**Symex time.** Five changes to goto-symex and util (`31e9ceed9a` to
`c15a94ab47`: `irept::compare` of shared data, guards at the end of
atomic sections, guard merging when the equation shares expressions,
symbol-table suffixes) bring the symex time of `TestNoRaceSelect4` at
`--unwind 3` from 199 s to 6.6 s without the race check, and from 479 s
to 25 s with it (build `47115f657c` against `63c2313a5c`); at
`--unwind 4`, with the check, from 778 s to 552 s. Rerun
with a timeout of 1,800 s, the 16 harnesses that timed out in K4 or ran
out of their bound gave:

| | Now | Build before the changes |
|---|---|---|
| `rwmutex.TestNoRaceRWMutexMultipleReaders` | clean, 1,318 s | timeout |
| `rwmutex.TestRaceRWMutexMultipleReaders` | race, 1,703 s | timeout |
| `sync.TestRaceAfterFunc3` | race, 1,488 s | race, 1,185 s |
| `mutex.TestNoRaceMutexPureHappensBefore` | unwind | unwind |
| 12 others | timeout | not run |

**Very large values.** Three changes, none specific to Go or to the race
check, remove the exponential cost of nested small arrays such as the
16 MB `[2][2]...[2]Point` (20 levels) of `TestRaceNestedArrayCopy`:

| Change | 2^20-element copy in C, without threads |
|---|---|
| The zero initializer flattens an array only if the elements that flattening copies, those of nested arrays included, are at most 1,000 (`c6fc82d8ba`) | 106 s to 2.5 s with `--no-array-field-sensitivity` |
| Field sensitivity divides an array only if its elements have at most 64² fields together (`85b1145528`) | out of memory (8 GB) to 18 s |
| Value sets skip array elements that equal the previous one (`63c2313a5c`) | 18 s to 5.6 s |

The profiling suite is unchanged by them (7.20 s of symex before, 7.23 s
after).

With the race check, the harnesses with large values still fail:

| Harness | K4 | Now |
|---|---|---|
| `TestRaceNestedArrayCopy` | out of memory after 93 s | out of memory after 1,389 s |
| `TestRaceMapBigKeyInsert` | error | out of memory (12 GB) in the SAT solver's preprocessing; race found in 514 s with `--no-sat-preprocessor` (36.7 million variables, 104 million clauses) |
| `TestRaceMapBigValInsert` | error | timeout (1,800 s) |
| `BigKeyAccess1`, `2`, `Delete`, `BigValAccess1`, `2` | race | race (same formulas) |

The cause is the encoding of shared objects under the memory model, not
the size of any one expression: each shared read of an object is a fresh
SSA symbol that equals one of the writes it may read from, and each such
equality covers the whole object. Without the race check,
`TestRaceMapBigKeyInsert` has 2,442 steps and 3 million variables. With
it, the flags of each access are read and written inside atomic sections,
so the 64,000-bit `[1000]*int` key and its shadow memory (typed as the
object, one byte of flag per byte) take part in many more such
equalities: 38,929 steps and 36.7 million variables. Making the
object-level flags (`OW`, `OR`, `RW`, `RR`) one byte per object removed
only 2.5% of the variables, and shadow memory as a flat array of bytes
did not change them; neither is committed.

## 14. Options for large values

Section 13 left three harnesses failing. Measuring where their variables
and time go, rather than the size of their expressions, found four
independent causes. Each was tried behind an environment switch (local
tag `exp/go-race-large-values`, commit `3ee4f040c2`). The fixes for
causes 1 and 2 are now committed; see the status at the end of this
section.

**Cause 1: writes that nothing reads are memory-model events.** Every
shared write gets a clock, a program-order constraint, and write-
serialization constraints against the other threads' writes to the same
address, even if no event reads that address. With field sensitivity, a
whole-object write is one event per field: in a 16 KB `[2]...[2]Point`
copy under the race check, 10,233 of 10,733 shared writes are to fields
that no event reads. Such a write cannot change what any read observes:
given an order of the other events, it can be placed right after its
predecessor in its thread (or in its atomic section), and nothing orders
it otherwise. Leaving these writes out of the memory model (`MM_PRUNE`):

| | Without | With |
|---|---|---|
| 16 KB nested copy, race check | 947,631 variables, no result in 1,500 s | 408,566 variables, 1 s of solving |
| Whole oracle, 310 harnesses whose verdict both runs agree on | 23,086 s | 6,916 s (all three switches; `MM_PRUNE` alone gives nearly all of it) |
| 16 harnesses (8 with the largest gains, 8 at random) | 2,541 s | 740 s (`MM_PRUNE` only) |
| `cbmc-concurrency`, Go and shadow-memory regression suites | pass | pass |
| Profiling suite, concurrency benchmarks | | unchanged (up to 3% fewer steps) |

No verdict of the oracle changes (runs K9 and K8, same build). This is
not specific to Go or to the race check.

**Cause 2: shadow memory repeats symbolic execution.** Shadow memory is
typed as the object it shadows, so a map header's shadow has a pointer
field `keys`; set_field writes flag bytes into it, its value set grows,
and the shared-value summary repeats symbolic execution. In the repeated
run, the goroutine starts with the summary's values, which include the
keys array it allocates later itself. Its search loop then reads that
array at indices 0 to 3, and the array theory instantiates 64,000-bit
elements for five indices instead of one. Leaving shadow memory out of
the summary (`SV_NOSM`; shadow values are never dereferenced) removes
the repetition. The repeated run's assumption that a thread may read its
own later writes is a separate imprecision, not addressed.

**Cause 3: shadow memory of dynamic arrays has the elements' width.** The
shadow of `keys` (an array of symbolic size) is an array of
`[1000]*int`, so each index of it costs 64,000 bits in the array theory.
An array of bytes instead (`SM_A`) has 8-bit elements. Together with
`SV_NOSM`, a standalone `map[[1000]*int]int` race goes from 36.7 million
to 4.5 million variables. Bytes do not help objects of constant size:
there, field sensitivity divides the typed shadow, and a byte array of
16 KB was slower (`SM_B`). Object-level flags (`OW`, `OR`, `RW`, `RR`)
of one byte per object (`SM_OBJ`) changed little once `MM_PRUNE` drops
the unread fields.

**Cause 4: arrays of constant size are always bit vectors.** `cbmc`
defaults to `--arrays-uf auto`, but the solver factory maps only `never`
and `always`, so `auto` behaves as `never` (also on `develop`): every
array of constant size, 16 MB included, becomes a bit vector, and every
equality between two versions of it (each shared read has some) costs
its full width. Using the array theory for arrays wider than 1,000 bits
(`ARRAYS_AUTO`, which also stops allocating literals for such arrays):

| | Before (run K9: 8 GB, 600 s) | With `ARRAYS_AUTO` and the other switches (14 GB, 1,800 s) |
|---|---|---|
| `[1<<20]Point` (16 MB, one dimension) copied while a goroutine writes it | out of memory (8 GB) | race in 0.5 s of solving, 97,000 variables |
| `BigKeyAccess1`, `2`, `Delete` | 380 to 451 s | 1 to 2 s |
| `BigKeyInsert` | out of memory | race in 6 s |
| `BigValAccess1`, `2`, `BigValInsert` | 122 s, 123 s, out of memory | invariant failure: a struct with a large array member |
| `TestRaceNestedArrayCopy` | timeout | error after 1,748 s |
| `regression/cbmc` | pass | 3 of 1,191 fail on invariants (a member of a member as an array, and the width of the array's literals) |

The array theory represents nested arrays as arrays of bit vectors, so
`[2][2]...[2]Point` still costs the width of each sub-array per index.
Linearizing nested arrays of constant size (an index `a[i][j]` as
`a'[i*n + j]`) is what `TestRaceNestedArrayCopy` additionally needs.

**Also tried:**

- SMT back ends on the `map[[1000]*int]int` race: Z3 ran out of memory
  at 23 GB, Bitwuzla answered unknown (it does not fully support
  equalities over constant arrays), cvc5 rejected the input.
- CaDiCaL instead of MiniSat: faster on the 16 KB nested copy (546 s
  against more than 1,500 s for the first call), slower on smaller ones
  (7 s against 2 s), and no better on the map race.
- MiniSat's preprocessing runs out of memory on formulas of about 100
  million clauses; `--no-sat-preprocessor` let `BigValInsert` finish in
  430 s and `BigKeyInsert` in 514 s. Causes 2 and 3 shrink these
  formulas instead.

**Status.**

- Cause 1 is fixed by `12fde03b52`: writes to addresses that no event
  reads are no events of the memory model, and traces keep them in their
  place (`regression/cbmc-concurrency/unread-shared-writes1`).
- Cause 2 is fixed by `bf65092c0b`: `is_shared_object` excludes shadow
  memory (`regression/go/race-map-insert1`).
- Cause 3 is not fixed. Bytes help the race check, which flags at most 64
  bytes per byte and larger accesses at the object, but hurt other users
  of shadow memory: a C program that sets the field of a whole 1,024-byte
  element of a malloc'd array, at a symbolic index, needs 1,024 updates
  of a byte array instead of one of an element. With `long` elements it
  ran out of memory (8 GB) instead of taking 137 s; with pointer elements
  it took 96 s and 17.9 million variables instead of 0.4 s. After the two
  fixes, bytes changed only the two insertion harnesses
  (`BigKeyInsert`: timeout at 1,200 s to a race in 1,092 s;
  `BigValInsert`: error to timeout).
- Cause 4 is a project of its own.

Run K10, the whole oracle with `bf65092c0b` (8 GB, 600 s), against run
K9 (the build before these fixes, same limits): no verdict changed apart
from `BigKeyInsert` (error to timeout) and
`TestNoRaceMutexPureHappensBefore` (timeout to unwind, after 306 s); the
310 harnesses with the same conclusive verdict took 7,245 s instead of
23,086 s; all 341 took 18,905 s instead of 35,832 s.
