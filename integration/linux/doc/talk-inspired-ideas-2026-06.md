# Ideas from the "kernel security in the age of AI" talk

Notes taken while reading `~/jakub_talk.txt` (a talk inspired by the
copyfail LPE — the CVE this whole bug-finding effort started from).  Three
concrete directions for the CodeQL→CBMC pipeline; one prototyped.

## 1. A "needle" provenance oracle for the copyfail class (PROTOTYPED)

The discovery insight that found copyfail was an operator hypothesis, not
a generic scan: *"splice can deliver page-cache references of read-only
files (incl. setuid binaries) into crypto TX scatter-lists."*  That is a
**page-provenance** observation — a read-only / shared page-cache page
reaching an in-place write sink — which is a different axis from our
input-taint layer (`KernelTaint`), and exactly the class the talk says is
now being "milked" (a wave of page-cache-overwrite bugs after Dirty Pipe
and copyfail).

Prototype: `pagecache_write_inplace.ql` + `copyfail_test.c`.  It flags a
function that (a) obtains a scatterlist from a user `iov_iter`
(`extract_iter_to_sg` / `iov_iter_extract_pages` / `af_alg_make_sg` —
pages that may alias read-only page-cache pages), and (b) issues an
in-place crypto write (`*_request_set_crypt` with src == dst) with no
intervening copy into a private buffer.  Synthetic: `aead_inplace_buggy`
flagged; `aead_inplace_fixed` (src≠dst + private copy) and
`aead_inplace_kernel_buf` (no iov provenance) excluded.

Honest scope limit (documented in the query header): the af_alg instance
behind copyfail is subtler — src and dst are *distinct* sgl objects that
can **alias** the same physical pages when user space reuses one buffer
for send+recv; that aliasing is not syntactically visible.  This oracle
catches the explicit `src == dst` in-place shape; the aliasing case needs
stage-2 (CBMC) or manual review.  Next step: build a CodeQL DB over
`crypto/ lib/scatterlist.c fs/splice.c` and run it on real af_alg /
algif_{aead,skcipher} code.

## 2. "BPF-checkpoint" assumption bisection → CBMC cover probes (IMPLEMENTED)

The talk reports that stronger models validate a claimed path A→B→C→D by
*sprinkling BPF probes* at the intermediate nodes to confirm each hop is
actually reached, then bisecting where an assumption breaks.  CBMC's
`cover` statements / reachability assertions are the static analogue: given
an oracle's (or an LLM's) claimed call/data path to a vulnerable sink,
auto-insert `__CPROVER_cover` / `assert(0)` probes at each intermediate
program point and let CBMC report which hops are feasible.  This turns a
narrative "this is reachable" into a machine-checked reachability witness
and pinpoints the first infeasible hop.  Good fit for our stage-2
harnessing infra (`oracle_harness_gen.py`).

**Implemented** (`cover-probe-bisection-2026-06.md`): `cover_probe.{h,py}`
+ `cover_bisect_test.c` + a real CVE-2026-31622 demo
(`real_nfc_llcp_cover.c`) where the OOB-read precondition is REACHABLE in
the vulnerable parser and BLOCKED by the fix.

## 3. Exploitability/impact axis on the confidence tier

The talk's central defender message: the bottleneck is no longer *finding*
bugs but *triaging which are real, exploitable, and urgent* — and LLMs
systematically overstate impact (claim LPE, deliver DoS).  Linus's
security@kernel.org note and the "AI-assisted CVE re-evaluation" project
both point the same way.  We just added a precision tier (HIGH/MEDIUM
raw-taint reachability) to the count/index + TLV oracles; a complementary
**impact axis** would classify each candidate by primitive strength:
OOB-**write** / control-data overwrite (high) vs OOB-**read** / null-deref
/ DoS (low).  Combined, (precision × impact) gives a triage ranking that
matches what the talk says defenders actually need.  Most of our oracles
already distinguish read vs write shapes, so the signal is largely present
— it just needs to be surfaced as a second tier dimension.

## Cross-cutting

Both the hammer (broad scan — our oracle suite) and needle (specific
hypothesis — idea 1) approaches are reported to work; the differential
PR-delta scanner we already built (`pr_delta_scan.py`) is the same
direction as the upstream commit-scanning bot the talk mentions, with the
noted improvement of attaching findings to the change under review.
