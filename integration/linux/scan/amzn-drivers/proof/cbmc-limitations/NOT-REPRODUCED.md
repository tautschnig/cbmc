# Two claimed tool issues that did not survive a reproducer

Both were asserted in earlier commit messages and docs on the strength of
behaviour seen in the ENA proofs. Neither could be demonstrated in a minimal
case, so neither is reportable. Recorded here with the attempts so that
someone can take them further, and so the claims are not quietly left
standing.

## Claim 1: `__CPROVER_object_whole` on a field address widens to the whole enclosing object

**Asserted in** `e93f9b8d6df` and `proof/fbsd/README-fbsd-build.md`, as the
explanation for why `__CPROVER_object_whole(&tx_ring->tx_stats)` in the
FreeBSD cleanup assigns clause caused `tx_ring->ena_com_io_sq` to be reported
`INVALID` in the cleanup tail.

**Not reproduced.** `dfcc_object_whole_field_NOT_reproduced.c` names
`__CPROVER_object_whole(&s.a)` in a loop's assigns clause and asserts that the
sibling field `s.b` survives: it does, SUCCESS.
`dfcc_object_whole_field_ptr_NOT_reproduced.c` repeats it in the FreeBSD shape
-- a struct field reached through a pointer parameter, with a sibling
*pointer* field -- and `ring->sq` survives too, SUCCESS.

**What is still true:** replacing
`__CPROVER_object_whole(&tx_ring->tx_stats)` with `tx_ring->tx_stats` was the
single token that made the FreeBSD failure go away, so the change was causal.
The mechanism is **unexplained**. Candidates not yet checked: `tx_stats` may
sit in one of `struct ena_ring`'s anonymous unions, which would make
`&tx_ring->tx_stats` a `byte_extract` expression whose CAR covers something
wider than intended -- the anonymous-union opacity already documented for the
DPDK proof; or the counter fields inside `tx_stats` are `counter_u64_t`, i.e.
pointers, which may change how the CAR is built.

The earlier claim that this is "the same trap as `make_wf_mbuf` and the
`llq_info` havoc" is **wrong**: those two are about
`__CPROVER_havoc_object`, whose whole-object behaviour is documented and was
confirmed, not about `__CPROVER_object_whole` in an assigns clause. Conflating
them is what made the explanation sound settled.

## Claim 2: `__CPROVER_forall` is silently ignored in a loop-contracts file

**Asserted in** earlier notes as the reason the FreeBSD ownership invariant
had to be written out over the concrete ring, and repeated as a reason to
raise a tool issue.

**Not reproduced.** `loop_contract_forall_NOT_reproduced.{c,json}` supplies an
invariant containing
`__CPROVER_forall { unsigned _j; _j < 4 ==> g[_j] == 0 }` from a
`--loop-contracts-file`. No "ignoring forall" warning is emitted, the contract
is applied (7 loop-contract properties on the right loop, all SUCCESS), and
the proof goes through. The test does not discriminate, though: the
post-condition also passes with the quantified clause removed, so it shows
only that nothing warns and nothing breaks -- not that the clause does work.

**What is still true:** the concrete-ring form of the ownership invariant was
needed, but the reason established by experiment is the *probe-index*
limitation -- an invariant assumed at one nondeterministic index constrains
only that index, while the loop body frees slot `req_id`. That is a property
of single-index probing, not evidence about `__CPROVER_forall`. A grep for
"ignoring forall" in `src/` finds nothing, so the warning text quoted in the
earlier note is not in the current tree.

## The pattern worth noting

Both claims came from a real observation plus a plausible mechanism, and in
both cases the mechanism was invented rather than tested. The same thing
happened with the DFCC quadratic-cost analysis, the anonymous-union
explanation of the `assigns` failures, and the `malloc`/`free` diagnosis on
FreeBSD -- each time a correct observation acquired a wrong cause. The cheap
guard is the one used here: write the three-line reproducer before writing the
explanation down.
