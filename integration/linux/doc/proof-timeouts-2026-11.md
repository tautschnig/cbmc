# Where the proof timeouts went

Every timeout in the ENA proofs, investigated by measurement rather than by
guessing a mechanism. Tooling: `proof/cbmc-limitations/measure_phases.sh`
(per-phase runtimes and sizes from one cbmc run), stack samples of the live
process with gdb, `--show-points-to-sets --json-ui` for value sets, and
`proof/fbsd/solver_hardness_summary.py` for clause attribution.

**Outcome.** One CBMC fix, one CBMC option, two harness mistakes. The
queue-start base case, which closes the induction, now verifies in about a
minute. The FreeBSD cleanup proof drops from 28M to 1.4M clauses. The DPDK
cleanup at ring 8 is still open, and it is solver-hard rather than
pathological.

## Summary

| proof | before | after | what changed |
|---|---|---|---|
| `ena_queue_start` base case, ring 8 | conversion never finished | **SUCCESSFUL in 67 s**, 158K variables / 18K clauses | harness: removed a sub-object havoc; CBMC option `--max-field-sensitivity-array-size 256`; CBMC fix `4cb211e9533` |
| FreeBSD cleanup, loop contract, ring 4 | 8.7M / 28.2M, solver 137 s | 3.2M / **1.4M**, solver **8.6 s** | `--max-field-sensitivity-array-size 256` |
| FreeBSD cleanup, trace macro live | 30.3M / 134.9M, no verdict | 3.1M / 1.2M, verdict in **28 s** | same option; the `ena_trace` neutralisation is no longer needed for cost |
| DPDK cleanup, unwinding, ring 8 | timeout | timeout | still open, see below |
| DPDK cleanup, loop contract, ring 4 | symex never terminated | completes in 140 s, contract incomplete | invariant for `pkt_cnt`; see below |

## 1. The quadratic in `convert_byte_update` (CBMC fix)

Stack samples of the queue-start run, taken during "converting SSA", were in

```
bv_endianness_mapt::build_little_endian
  <- bv_pointerst::endianness_map
  <- boolbvt::convert_byte_update     (boolbv_byte_update.cpp:89)
```

For a `byte_update` at a non-constant offset, `convert_byte_update` loops over
every byte offset of the updated object, and **inside the loop** built two
endianness maps from the operand and value types. Neither depends on the
offset, and building one is linear in the width of the type, so the
conversion was quadratic in the object size: for an N-byte object, 2N maps of
8N entries. Hoisting them out of the loop is the whole fix (`4cb211e9533`):

| bytes | convert SSA before | after |
|---|---|---|
| 2048 | 0.17 s | 0.09 s |
| 4096 | 0.53 s | 0.18 s |
| 8192 | 1.80 s | 0.36 s |
| 16384 | 8.79 s | 0.71 s |

4x per doubling before, 2x after; identical formula and verdict.
`regression/cbmc` gives the same failing set before and after. This alone did
not make the base case finish -- the encoding it was building was itself
enormous, for the reasons in section 2 -- but it is a genuine CBMC
performance bug and worth upstreaming independently.

## 2. The field-sensitivity array limit (CBMC option)

**This is the main finding.** After the fix above, the queue-start run still
did not finish converting. The SSA had 12,193 `byte_update`s, 9,744 over the
whole `pool_tbi` array and 2,248 over the whole 182,144-byte adapter, at
symbolic offsets, from source lines that are not on the queue-start path at
all: the devargs parsers (`ena_process_uint_devarg`), reached through CBMC's
function-pointer expansion at the mempool enqueue call; the RX branch of
`ena_queue_start`; and `rte_write32_relaxed`.

`--show-points-to-sets` gave the first wild dereference in symex order:

```
byte_extract_little_endian(adapter..tx_ring[0].$anon0, 0, uint16_t *)
   -> 16 targets, including `unknown` and `integer_address`
```

That is `ring->empty_tx_reqs`, read back in `ena_create_io_queue`'s
free-list initialisation `ring->empty_tx_reqs[i] = i`, although the harness
had stored exactly `pool_etr` into it. With `unknown` in the set, that loop is
a write to anything, it clobbers `ring->type`, the TX early return is no
longer decided during symex, and everything after cascades.

The cause is **not** the anonymous union, which was the hypothesis carried
since stage 1. It is the array the ring sits in. `struct ena_adapter` holds
`tx_ring[ENA_MAX_NUM_QUEUES]` with 128 entries, and CBMC tracks the fields of
array elements individually only for arrays of at most 64 elements
(`DEFAULT_MAX_FIELD_SENSITIVITY_ARRAY_SIZE` in `util/magic.h`).
`cbmc-limitations/field_sensitivity_array_limit.sh` shows the threshold
exactly:

| rings in the array | anonymous union | points-to set of the read |
|---|---|---|
| 64 | yes | 1 (the stored object) |
| **65** | yes | **7, including `unknown` and `integer_address`** |
| 65 | **no** | **7** -- the union is not involved |
| 65 | yes, with `--max-field-sensitivity-array-size 256` | 1 |

The same limit explains the FreeBSD cost: `ena_com_dev` holds
`io_sq_queues[256]` and `io_cq_queues[256]`. With the limit raised the FreeBSD
cleanup instance falls from 28.2M to 1.4M clauses with the trace macro
neutralised, and from 134.9M to 1.2M with it **live** -- so the earlier
attribution of that cost to the `uintptr_t` cast in `container_of` was at best
partial. The FreeBSD property set is identical with and without the option
(1487 properties, zero differing ids, all SUCCESS).

The option affects precision only, not soundness: a field-insensitive value
set is a superset. The runners now pass `--max-field-sensitivity-array-size
256`. It does cost symex time -- 14,828 to 205,894 steps for FreeBSD -- but
that is seconds against minutes saved in the solver.

Worth reporting upstream: the cliff is silent. Nothing tells the user that a
struct inside a 65-element array has lost field sensitivity, and the symptom
appears far away as `unknown` in a points-to set and a cascade of whole-object
`byte_update`s. A warning when a pointer is read from an element of an array
above the limit would have saved most of this investigation.

## 3. Two harness mistakes

**`__CPROVER_havoc_object` on a sub-object.** The queue-start harness did
`__CPROVER_havoc_object(&adapter.ena_dev.io_cq_queues[0])` intending to havoc
one CQ. It havocs the whole enclosing object -- all 182,144 bytes of
`adapter`, every pointer included (`cbmc-limitations/havoc_subobject.c`
confirms the semantics). `io_cq->numa_node_cfg_reg` was then a wild pointer
and `ena_com_update_numa_node` wrote through it. Replacing the havoc with
nondeterministic assignment of the two scalar fields that needed it took the
SSA from 48,845 to 31,251 steps and the `byte_update`s from 12,193 to 2,595 --
but not to completion, because section 2 was still in force. This is the
third time the whole-object semantics of `havoc_object` has caught this
project; it is documented, the mistake is ours.

**An unconstrained `pkt_cnt` in the DPDK cleanup loop contract.** The contract
put `pkt_cnt` in the frame without an invariant for it. The fast-free tail
after the loop calls `rte_pktmbuf_free_bulk(pkts_to_clean, pkt_cnt)`, which
then iterated over a havocked count; symex unwound it 7,738 times and never
finished. `pkt_cnt <= total_tx_pkts` and `mbuf_cnt < 256` fix that, and the
run completes in 140 s at 2.6M clauses -- but it reports failures that are the
contract's, not the driver's: `ena_trigger_reset` writes `adapter->reset_reason`
and `adapter->trigger_reset`, which are not in the frame; three step clauses
are not inductive; and `pkts_to_clean[pkt_cnt]` can go out of bounds because
nothing yet relates `pkt_cnt` to the array size. That contract is not needed
for ring 4, where the unwinding proof is already complete; it is kept for
widening.

## 4. `--slice-formula`

Not used by any runner before this. Measured on every proof, with the verdict
and property set checked to be identical:

| proof | default | with `--slice-formula` |
|---|---|---|
| FreeBSD cleanup | 8.67M var / 28.2M cl, solver 137 s | 6.44M / 27.7M, solver 128 s |
| FreeBSD cleanup, limit 256 | 3.18M / 1.37M, 8.6 s | 0.94M / 0.88M, 5.7 s |
| queue start, limit 256 | 158K / 18.4K | 6.0K / 12.4K |
| DPDK cleanup ring 8 | 5.04M / 20.6M, timeout | 3.02M / 13.3M, timeout |
| DPDK cleanup ring 8, limit 256 | 5.13M / 20.9M, timeout | 2.34M / 10.6M, timeout |

It consistently removes a quarter to three quarters of the variables, and up
to half the clauses, at negligible cost (slicing 7,100-10,900 assignments).
It turned no timeout into a verdict, and in the one run that was slow in the
solver it saved about 7%. Worth enabling: never harmful here, sometimes a
third off the solver time. It is not a substitute for finding the cause --
section 2 is a 20x reduction, `--slice-formula` a 1.1-2x one.

## 5. Still open: the DPDK cleanup at ring 8

Neither fix touches it, and the evidence says it is genuinely hard for the
solver rather than pathological in the encoding. Conversion takes 8 s; stack
samples sit in CaDiCaL's `propagate` and `analyze` for the full 40 minutes, so
it is in search. At ring 4 the solver takes 122 s on 6.5M clauses, 97% of which
come from six source lines of `ena_tx_cleanup`: the reads and writes through
`tx_info = &tx_ring->tx_buffer_info[req_id]` (lines 3230, 3232, 3235, 3245,
3248) and the final `comp_ack` branch (3256). Each iteration accesses an array
of 304-byte structures at a device-chosen index, so the encoding grows with
ring size times iterations, and the search with it.

`INV_DESC_SUM` -- the obvious suspect, being a sum over the whole ring -- is
**not** the cost: at ring 4, 130 s with it and 116 s without (the build
without it fails `INV_STARTED`, as expected, and is for measurement only).

The route forward is the loop contract of section 3, completed, which would
replace the ring-size unrolling by one symbolic iteration.

## The pattern, once more

Three hypotheses carried into this investigation were wrong: that the
anonymous union caused the wild pointers, that `INV_DESC_SUM` dominated the
cleanup cost, and that the FreeBSD clauses came from the `uintptr_t` cast. All
three were refuted by a measurement that took minutes. The two causes that
held up -- the quadratic map construction and the field-sensitivity cliff --
were found by looking at a stack sample and a points-to set, not by reasoning
from the source.
