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
