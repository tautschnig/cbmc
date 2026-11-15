# Claims that a later check overturned

A ledger, kept so the base rate is visible rather than remembered. Each entry
is a statement written down as fact and later shown wrong, or caught just
before it would have been.

| # | claim | how it was refuted | caught |
|---|---|---|---|
| 1 | Bitwuzla succeeds where Z3/CVC5 crash because it uses a native API binding | reading `smt2_dec.cpp`: every SMT2 backend is a child process fed the same text; the difference is `use_datatypes` in the encoding | after commit, by the user |
| 2 | SMT2 CPU is under-reported because the child solver's CPU is not attributed | grouping the benchmark CSV by outcome: every completed run has wall/CPU 1.00; only killed runs lose the child's rusage | after commit |
| 3 | `--cprover-smt2` returns ERROR on byte-extracts over anonymous unions | a one-line `int` program gave the same ERROR; `smt2_solver` was not on `PATH` | after commit |
| 4 | F14's mechanism is refuted; "F23" is a new defect | the existing `harness_doorbell_budget.c` assumes `BUDGET_FITS_PACKET` in every configuration; under it F14 holds and "F23" is the excluded degenerate case | after commit; and the same wrong claim had been made and withdrawn once before |
| 5 | the cleanup-step transcription caveat was not written down anywhere | `ena-cleanup-ring8-2026-11.md` had said so since the artefact was written | after commit |
| 6 | eth_ena_dev_init leaks the IRQ callback on the polling-mode failure path | reading the branch: the callback is registered only in the other branch | before writing |
| 7 | FreeBSD `ena_rx_mbuf` indexes `rx_buffer_info` with an unvalidated device `req_id` | `ena_com_rx_pkt` rejects `req_id >= q_depth` one layer down | before writing |
| 8 | a datapath call racing `ena_stop` divides by zero and underflows | the harness's teardown model zeroed `q_depth`; the real `ena_com_io_queue_free` does not | before writing |
| 9 | nothing in the corpus asserts a class S property | `F18_SETUP_RC` and `CFG_AENQ_FAIL` already do | before writing |
| 10 | `check_hygiene.sh`: a reached bodyless callee "cuts the path" | a three-line program: plain CBMC reports `no body for callee` and the next `assert(0)` still FAILS; only DFCC cuts (already recorded in `cbmc-limitations/dfcc_bodyless_callee_cuts_path.sh`) | after commit |
| 11 | `harness_map_segs.c` proves the descriptor bound for every legal chain | a new witness "the longest legal chain fills sgl_size - 1 descriptors" PASSED: with 64-byte segments and a 96-byte LLQ push header the first segment never got its own descriptor, so that branch was never exercised | after commit |
| 12 | the asm-semantics slowdown is an expression-simplifier gap | the test program behind it was wrong: symex does not substitute non-constant values, so the simplifier never saw the composed term. The `-DWIDE` control in `asm_spinlock_unwinds.c` isolates the sub-register merge in the asm lowering | before writing |

**Seven of twelve were caught only after they were committed, and three of
those seven by someone else.** The five caught in time were all caught by the
same two moves. Claims 10 and 11 were caught by running the gate and adding a
witness, not by reading.

## The two questions, before writing a mechanism or an absence

1. **A mechanism** ("X happens because Y"): is there a program small enough
   that only Y can explain it, and has it been run? Claims 1, 2, 3 and 8 each
   fell to exactly such a program within minutes. If there is no reproducer
   yet, the sentence says "suspected", not "because".

2. **An absence** ("nothing does X", "X is not documented", "this is new"):
   has the claim been checked with a search, and was the search wide enough
   to include the artefact-specific note, the harness index, and the existing
   runners? Claims 4, 5 and 9 were all absences, and all three were refuted by
   a file that already existed in the tree. `doc/harness-index-2026-11.md` and
   `rg` across `proof/` and `doc/` are the minimum.

## Why this is a ledger and not a gate

The proof hygiene rules became `check_hygiene.sh` because a log either
supports a claim or does not, which a script can decide. Whether a sentence in
a commit message is a mechanism or an absence is not something a script can
decide reliably, so this stays a checklist. What the ledger adds is the count:
if the "after commit" column keeps growing, the checklist is not being used.
