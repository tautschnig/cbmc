# Targeting the latest mainline — feasibility (#2)

**Date:** 2026-06-08
**Question:** can we run the sweeps on the newest tree so a hit is a
genuine *unpatched* finding rather than a stable-tree rediscovery?

## Feasibility: CONFIRMED

| factor | result |
|--------|--------|
| network to git.kernel.org | OK (TCP 443; stable at v6.12.92, mainline tags visible) |
| latest mainline obtainable | **yes** — `git clone --depth=1` of torvalds/linux fetched **v7.1-rc7** in minutes (shallow, no history) |
| disk | fine (1 TB, ~49% used) |
| recipe portability | `build-codeql-db.sh` works unchanged — the `percpu.h` `CONFIG_CC_HAS_NAMED_AS` gate is still at line 35 in 7.1 |
| scoped subsystem build | fast (net/nfc DB built in ~30 s) |

Tree now at `/home/ubuntu/linux_mainline` (7.1-rc7).  Older local
trees (5.10/6.1/6.6/6.12) are all behind; `linux.git` is 5.12-rc3.

## Sweep result on mainline 7.1-rc7

`tlv_parse_loop.ql` on the mainline net/nfc DB returns the **same
walkers, still unpatched**:

```
nfc_llcp_parse_gb_tlv          llcp_commands.c:203
nfc_llcp_parse_connection_tlv  llcp_commands.c:253
nfc_llcp_recv_snl              llcp_core.c:1313   (was :1300 in 6.12)
nfc_llcp_connect_sn            llcp_core.c:855    (was :853 in 6.12)
```

`nfc_llcp_parse_gb_tlv` in 7.1-rc7 is verbatim the buggy form
(`u8 offset`, `while (offset < tlv_array_len)`, `tlv[0]/tlv[1]` with
no header-fits check).

`drivers/staging/rtl8723bs/` is also still present in 7.1, so the 7
IE-walker candidates from #3 apply to mainline too.

## Honest interpretation

* **The methodology now targets the freshest tree** — fetching
  mainline and sweeping a subsystem is a few-minutes operation, so
  future runs need not be on a stale stable snapshot.
* **These specific NFC candidates are present/unpatched in the
  7.1-rc7 snapshot, but they are NOT a novel discovery.** The
  CVE-2026-31622 fix is in-flight on the netdev/stable lists (per the
  patch postings found earlier) and simply has not landed in Linus's
  tree in this snapshot.  So "unpatched in mainline-rc" here means
  "fix pending merge," not "previously unknown."
* The value delivered is the *capability*: the pipeline runs on
  latest mainline, distinguishes fixed-vs-present, and would flag a
  genuinely new walker the moment one is introduced — without the
  rediscovery ambiguity of an old stable tree.

## Recommended standing practice

1. `git clone --depth=1 …torvalds/linux` (or `git fetch --depth=1`
   to refresh) — cheap.
2. `build-codeql-db.sh <tree> <db> <subsystem>` — percpu.h-safe.
3. Run `tlv_parse_loop.ql` + `tainted_into_fixed_dest.ql` +
   `tainted_alloc_overflow.ql`.
4. For any hit, diff against `linux-next` / the netdev list to
   confirm it is not already a pending fix, then drive the generated
   CBMC harness and the validation loop.

linux-next (a separate, larger remote) would push novelty further
still; the shallow-clone approach here makes that a bandwidth/time
choice, not a blocker.
