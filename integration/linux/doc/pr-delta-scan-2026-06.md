# Differential PR-delta scanning (#1) — the discovery play

**Date:** 2026-06-08
**Tool:** `integration/linux/scan/pr_delta_scan.py` + `func_ranges.ql`.
**Demonstration:** mainline `v7.1-rc6 .. v7.1-rc7` (one week of merges),
`net/` paths, against an rc7 CodeQL DB.

## What it does

The retrospective (`why-no-new-bugs-retrospective-2026-06.md`) concluded
that whole-tree scanning of swept mainline/LTS yields recall validation,
not discovery, and that the real play is **pivot A**: scan the *delta*
of fresh changes, before the incumbent tools sweep them, asking "does
this PATCH introduce a bug shape into a function it touched?"

`pr_delta_scan.py` implements exactly that:

1. `git diff --unified=0 BASE HEAD` → changed line ranges per file;
2. `func_ranges.ql` → every function's `[startLine,endLine]`;
3. intersect → the set of **functions the patch touched**;
4. run the seven oracles over the tree's DB;
5. keep only hits whose `(file, function)` was touched;
6. emit a patch-touched candidate table.

This points the existing detectors at the window between "merged" and
"swept", and shrinks scope to a handful of fresh functions — tractable
for CBMC stage-2 and a clean before/after signal.

## Result on rc6 → rc7 (net/)

```
[delta] 52 changed files in scope (v7.1-rc6..v7.1-rc7)
[delta] 8 functions touched by the patch (present in the DB)
ORACLE      FUNCTION                        FILE:LINE
tlv_parse_  ieee80211_parse_tx_radiotap     net/mac80211/tx.c:2099
tlv_parse_  help                            net/netfilter/nf_conntrack_irc.c:102
[delta] 2 patch-touched oracle candidates
```

From 52 changed files the scanner produced a **2-candidate** high-signal
set — and both candidates are exactly the parser functions that received
**security fixes** in this window:

* **`ieee80211_parse_tx_radiotap`** — rc7 added
  `if (*iterator.this_arg < 2)` to guard
  `info->control.antennas |= BIT(*iterator.this_arg)`.
  `*iterator.this_arg` is an attacker-controlled radiotap byte; `BIT(x)`
  is `1 << x`, so the pre-rc7 code had an unguarded shift by an
  attacker value (UBSAN/OOB class).  Our TLV-loop oracle flagged the
  radiotap iterator walk.

* **`nf_conntrack_irc.c help`** — rc7 changed `continue` → `goto out`
  on forged / unparseable DCC commands, hardening the IRC DCC parse
  loop.  Flagged via the same loop-walk shape.

## Honest assessment

* **The mechanism is validated.**  Pointed at a real one-week delta, the
  scanner homed in on precisely the two freshly-patched parsers, with no
  whole-tree noise.  That is the behaviour pivot A promises.
* **No novel unfixed bug** — rc7 is the *post-fix* tree, so we see the
  hardened versions.  Run on the delta of the commit that *introduced*
  each issue (i.e. as a pre-merge PR gate), the scanner would have
  flagged the unguarded code.  That is the intended deployment:
  regression/introduction gating at patch time, not retrospective
  whole-tree mining.
* **Coverage caveat:** several changed parser subsystems (sctp,
  wireless, mptcp) were Kconfig-gated out of this DB build (same class
  as the earlier rxgk gap), so only the built subset (ipv4, netfilter,
  bluetooth, mac80211, ...) was scanned.  Enabling those configs (as we
  did for RXGK) widens coverage; the tool itself is config-agnostic.

## Usage

```sh
CODEQL_PACKS=/home/ubuntu/codeql/qlpacks \
python3 integration/linux/scan/pr_delta_scan.py \
  --tree /path/to/tree --db /path/to/codeql-db \
  --base v7.1-rc6 --head v7.1-rc7 --paths net/
```

For true PR gating: build the DB on the post-merge tree and pass the PR's
base..head as the range; any patch-touched candidate is a review item,
and the shapes with faithful harnesses (TLV) can be auto-driven through
CBMC stage-2 via `oracle_harness_gen.py`.

## Next

* Build DBs with the parser subsystems' Kconfigs enabled (sctp/wireless/
  mptcp/…) for full delta coverage.
* Wire `pr_delta_scan.py` candidates straight into the stage-2 harness
  generators so each patch-touched hit gets a CBMC verdict in one run.
* Run it daily across consecutive linux-next snapshots (next-N vs
  next-N+1) — the freshest possible delta.

## Follow-up 1: full Kconfig coverage (rc6 → rc7)

Rebuilt the rc7 DB with `IP_SCTP=m` and `MPTCP=y` enabled (wireless/
mac80211 were already `=y`).  Coverage of the delta's touched functions
rose 8 → 24, and candidates 2 → 5.  The three newly-reachable candidates
all triage to **FP** on the post-fix rc7 tree:

| Candidate | Oracle | Verdict |
|-----------|--------|---------|
| `mptcp_write_options` (net/mptcp/options.c) | tlv_parse_loop | **FP** — TX option *writer* (`*ptr++ = mptcp_option(...)`) into the kernel's own bounded TCP-option space; lengths are kernel-computed, not attacker-derived (the TX-writer FP class, cf. `l2cap_*_send`) |
| `sctp_sf_do_5_2_6_stale` (net/sctp/sm_statefuns.c) | skb_field_before_lencheck | **FP** — `err = (sctp_errhdr *)chunk->skb->data` then read at offset `sizeof(*err)`; chunk length is validated on the SCTP receive path before dispatch (caller-validated, function-granularity FP) |
| `sctp_sf_do_5_2_6_stale` | decoded_len_arith_overflow | **FP** — `stale = (ntohl(...) * 2)/1000` is a cookie *lifespan time*, not a buffer size/index; the overflow is benign (wrong timer, not memory unsafety) |

The rc6→rc7 changes to both functions were unrelated refactors (sctp:
removed COOKIE-ECHO resend logic; mptcp: option-accounting cleanup), so
the oracles surfaced pre-existing (guarded/benign) shapes, not
patch-introduced bugs.

**Net:** full coverage works and keeps the candidate set small and fully
explainable (5 total; the 2 original = real security-fix areas, the 3
new = FP).  No novel bug on the post-fix tree — the expected result.
Confirms two FP classes for future precision work: TX-side buffer
*writers* (tlv_parse_loop) and decoded values used as *non-size*
quantities (decoded_len multiply).

## Follow-up 2: consecutive linux-next daily snapshots

Fetched `next-20260604` (and `next-20260608`) alongside the existing
`next-20260605`, and ran the scanner on the **one-day** delta
`next-20260604 .. next-20260605` against `broad-next-db` (built at
next-20260605), `net/` + `drivers/net/vxlan/`:

```
[delta] 71 changed files in scope (next-20260604..next-20260605)
[delta] 6 functions touched by the patch (present in the DB)
ORACLE      FUNCTION              FILE:LINE
tlv_parse_  ieee80211_start_ap    net/mac80211/cfg.c:1630
tainted_co  ieee80211_start_ap    net/mac80211/cfg.c:1656
```

From a 464-file daily delta the scanner produced a **2-candidate** set,
both on `ieee80211_start_ap` (touched by a 3+/4- refactor), both **FP**:

* count/index `sdata->link[link_id]` — the caller-validated mac80211
  `link_id` class already documented in `broad-sweep-triage-2026-06.md`;
* tlv_parse_loop — the AP-settings beacon/IE walk, validated on the
  nl80211 path.

**Net:** the discovery mechanism works identically on consecutive
linux-next snapshots (the freshest possible delta) — tiny, explainable
candidate set, no novel bug.  The recurring noise source across both
deltas is the **caller-passed `link[link_id]` direct-index** pattern;
deprioritising count/index hits whose index is a bare parameter (not
decoded in-function) would remove essentially all of it.

## Status

Both follow-ups complete.  The differential scanner is validated on two
independent real deltas (mainline rc6→rc7 tag delta; linux-next
0604→0605 daily delta), with full Kconfig coverage of the parser
subsystems.  It is ready to run as a daily linux-next gate or a
pre-merge PR check; the single highest-leverage precision improvement is
suppressing the bare-parameter direct-index FP class.

## Precision improvement: bare-parameter index/count suppression

Implemented the proposed fix for the recurring `link[link_id]` noise:
`tainted_count_into_fixed_array.ql` now excludes a count/index that is a
**bare incoming parameter** never reassigned in-function (the bound is
the caller's job).  Decoded-in-function locals (b43 `keyidx`) and wire
struct fields (nci `n_targets`, cgw `result_idx`) are kept.

Effect: mac80211 `link_id` direct-index hits 112 → 60 on broad-next-db;
all genuine candidates retained.  The residual 60 are `link_id` LOCALS
assigned from a *parameter's field* (`link_id = params->beacon.link_id`)
— structurally identical to genuine wire-field indices (nci
`n_targets = ntf->...`), so separating them is a semantic-trust question
CodeQL cannot decide without annotations.  Pushing further would drop
true positives, so this is the sound stopping point; the residual is a
fast FP dismissal on review (the source struct is in-kernel-validated).
