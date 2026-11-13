#!/usr/bin/env python3
"""Second pass over the 40 Unveil findings the keyword classifier left in Z.

The first pass (classify_unveil.py) matched keywords against each finding's
Impact column and put 40 of 181 in "Z other" -- the largest bucket, larger
than any class we have proven anything about.  Targets were being chosen
without knowing what was in it.

Forty is small enough to assign by reading rather than by pattern, so that is
what this does: every assignment below is explicit, attributed to the
finding's own wording, and reviewable line by line.  No keyword gets to decide
anything here.

Two things came out of the reading that matter more than the bookkeeping:

  * Most of Z is not new.  Twenty-four of the forty belong to classes we
    already track; the first pass missed them because the wording avoided its
    keywords ("blocks 30 s" rather than "hang", "keeps DMA addresses of freed
    host_info" rather than "DMA into freed memory").
  * The largest coherent group that the class list did not name is: an
    operation the device REJECTED is reported to the caller as SUCCESS.  Six
    findings.  It is NOT new to us -- F18_SETUP_RC and CFG_AENQ_FAIL are
    already obligations of exactly this shape, and F18 is our own finding of
    this class -- but it had no name, so nothing counted how much of Unveil
    sits in it.  Naming it is the useful part: the shape is known to work and
    simply needs applying across the set/get entry points.

  classify_z.py            print the classification
  classify_z.py --check    fail if any Z finding lacks an assignment
"""
import argparse
import json
import sys
from collections import Counter, defaultdict

SOURCE = "/tmp/unveil_classified.json"

# (substring that identifies the finding, class, one-line reason)
# The substring is matched against the Impact text; it is an identifier, not a
# classification keyword.
ASSIGN = [
    # ---- already-tracked classes the first pass missed ----
    ("keeps DMA addresses of freed host_info", "A",
     "the device retains DMA addresses of memory the driver freed"),
    ("Orphan device MR over returned GPU pages", "A",
     "device keeps an MR over pages already returned to the owner"),
    ("Freed admin queues left armed in the device", "A",
     "device still armed on freed admin queues -- the F25 shape exactly"),
    ("Kernel heap write past sub_cq_arr", "G",
     "out-of-bounds kernel heap write on an out-of-spec device answer"),
    ("blocks 30 s on a queue nothing completes", "E",
     "a verb blocks indefinitely; the wording avoided the hang keywords"),
    ("Orphaned NVIDIA page registration", "D",
     "page registration leaked when put_pages fails"),
    ("Orphaned Neuron VA registration", "D",
     "VA registration leaked when unregister_va fails"),
    ("Zero-copy AF_XDP rx work left in the CQ", "F",
     "work left in the completion queue, no progress until something else pokes it"),
    ("non-power-of-two max_eq_depth", "F",
     "lost or duplicated completion notifications"),
    ("failed ethtool -X table becomes the device's RSS table", "C",
     "a rejected configuration is retained and later committed"),
    ("keeps a rejected request's entries", "C",
     "rejected entries stay in the cache and a later write commits them"),
    ("RSS entries from a rejected sysctl write silently reach", "C",
     "same shape: rejected input reaches the hardware later"),
    ("trailing GET_STATS admin read", "C",
     "activity continues after the feature was turned off"),
    ("PHC stays o", "K",
     "one failed init disables the feature permanently, overriding the user"),
    ("Device recovery disabled for good", "K",
     "sticky wrong state after a failure followed by a success"),
    ("drops other queues' zero-copy and plain tra", "K",
     "one socket's lifecycle takes down unrelated queues"),
    ("timeouts cut short by firmware hints", "K",
     "admin queue closed on a device-influenced timeout"),
    ("Cached QP state diverges from the device", "K",
     "cached state disagrees with the device and bring-up is refused"),
    ("Racing modifies leave the cached QP state", "K",
     "same divergence, reached by a race"),
    ("interrupts still unmasked after an auto-poll fallback", "K",
     "interrupt and polling configuration left inconsistent"),
    ("phc_error_bound sysfs file missing", "K",
     "feature enabled by one interface is not visible through the other"),
    ("Probe succeeds with no admin completion or AENQ delivery", "K",
     "probe reports success half-initialised -- near F18 and F25"),
    ("Unregister of a never- registered devlink port", "B",
     "releasing something that was never acquired -- the obligation our "
     "ledger already calls a wild free (LK_NO_WILD, PR_NO_WILD)"),
    ("DMA at physical address 0", "H",
     "the device is pointed at physical address 0"),
    ("Invalid DMA unmap of address 0", "H",
     "unmap of an address that was never mapped"),

    # ---- the new class, with no obligation anywhere in our corpus ----
    ("reports success for a hash- field change the device rejected", "S",
     "ethtool reports success for a change the device refused"),
    ("MTU change reported as successful although the device rejected", "S",
     "MTU set returns success on a device rejection"),
    ("ethtool -G silently ignored after a memory-pressure", "S",
     "ring-size change silently ignored, success returned"),
    ("Rejected stats_sample_interval writes report success", "S",
     "sysctl write rejected by the device, success returned to scripts"),
    ("Netmap registration reports success with driver and kring", "S",
     "registration returns success with the two modes out of sync"),
    ("Lost AF_XDP wakeup reported as success", "S",
     "a lost wakeup is reported as success when it races ena_down"),

    # ---- response attributed to the wrong request ----
    ("Wrong admin response returned after abort and command-ID reuse", "R",
     "a response is matched to the wrong command after ID reuse"),
    ("returns another command's response when the device duplicates", "R",
     "a duplicated device completion is attributed to the wrong command"),

    # ---- the driver offers the device or the application malformed work ----
    ("exceed the device's LIMIT_TX_BURST byte limit", "P",
     "the driver exceeds a device-advertised limit -- the F14/F23 neighbourhood"),
    ("nb_segs larger than their mbuf chain", "P",
     "the application is handed an mbuf whose stated segment count is wrong"),

    # ---- not safety: cost, cosmetics, portability ----
    ("fails with ENOMEM on 64 KiB page kernels", "N",
     "a size calculation that does not fit large base pages; functional, not unsafe"),
    ("arms rx need_wakeup after every under-budget poll", "N",
     "needless wakeups; a cost bug"),
    ("Futile wakeup syscalls and empty NAPI polls", "N",
     "same, stated as the user-visible effect"),
    ("Wrong sq consumer count in admin error logs", "N",
     "a log field is wrong; no state is damaged"),
    ("First metrics sample after a re-enable runs", "N",
     "first sample lands early; a timing inaccuracy"),
]

NAMES = {
    "A": "A device DMA/CQ writes into freed memory",
    "B": "B lifetime: release of something never acquired",
    "C": "C stale / live resource after a failed path",
    "D": "D leak",
    "E": "E deadlock / hang / lockup",
    "F": "F lost event / stuck / no progress",
    "G": "G memory corruption / out-of-bounds",
    "H": "H NULL / invalid deref / invalid address",
    "K": "K unrecoverable / wrong state / outage",
    "S": "S success reported for an operation that failed or was rejected",
    "R": "R response attributed to the wrong request",
    "P": "P driver offers device or application malformed work",
    "N": "N not a safety class: cost, cosmetics, portability",
}

# Whether an obligation of the shape we already write can express the class.
REACH = {
    "A": "yes -- the ghost mapped-set in harness_dma_order.c is this class",
    "B": "yes -- LK_NO_WILD and PR_NO_WILD are already exactly this",
    "C": "yes -- error-path obligations, as in harness_setup_errorpath.c",
    "D": "yes -- the leak ledger, which found F25",
    "E": "yes -- the ghost lock counter in harness_lock_order.c",
    "F": "yes -- the doorbell/wakeup obligations, as in XS_WAKEUP",
    "G": "yes -- bounds checks, already on in every run",
    "H": "yes -- pointer checks plus the non-NULL invariant clauses",
    "K": "yes -- the lifecycle invariants and the restartability clauses",
    "S": "yes, and the corpus ALREADY has two of these -- F18_SETUP_RC "
         "(ena_setup_rx_intr returns 0 only if the interrupt was really set "
         "up) and CFG_AENQ_FAIL (a rejected AENQ configuration is not "
         "recorded as applied). F18 is our own finding of this class. So the "
         "shape is proven to work; what is missing is applying it "
         "systematically across the set/get entry points.",
    "R": "partly -- harness_admin_cq.c's AC_* obligations are the right "
         "shape; neither ID reuse nor duplicated completions is modelled yet",
    "P": "yes -- F14 and F24 are this class, proven by relating what the "
         "driver builds to what the device or application permits",
    "N": "out of scope, and should be said so rather than counted as a gap",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    try:
        items = json.load(open(SOURCE))
    except OSError:
        print(f"cannot read {SOURCE}; run classify_unveil.py first",
              file=sys.stderr)
        return 1

    z = [i for i in items if i.get("cls", "").startswith("Z")]
    out, unassigned = [], []
    for item in z:
        impact = item["impact"]
        for key, cls, why in ASSIGN:
            if key in impact:
                out.append((cls, item["comp"], impact, why))
                break
        else:
            unassigned.append(item)

    if args.check:
        if unassigned:
            print(f"{len(unassigned)} class Z finding(s) with no assignment:",
                  file=sys.stderr)
            for i in unassigned:
                print(f"  [{i['comp']}] {i['impact'][:120]}", file=sys.stderr)
            return 1
        print(f"all {len(z)} class Z findings assigned")
        return 0

    print(f"Class Z: {len(z)} findings, reassigned by reading\n")
    counts = Counter(c for c, _, _, _ in out)
    existing = sum(v for k, v in counts.items() if k not in ("S", "R", "P", "N"))
    print(f"  already-tracked classes, missed by keyword: {existing}")
    print(f"  new classes:                               "
          f"{counts['S'] + counts['R'] + counts['P']}")
    print(f"  not a safety class:                        {counts['N']}\n")

    for cls in sorted(counts, key=lambda c: (-counts[c], c)):
        print(f"{NAMES[cls]}  --  {counts[cls]}")
        print(f"  in reach? {REACH[cls]}")
        by = defaultdict(list)
        for c, comp, impact, why in out:
            if c == cls:
                by[comp].append((impact, why))
        for comp in sorted(by):
            for impact, why in by[comp]:
                print(f"    [{comp}] {impact[:100]}")
                print(f"        -> {why}")
        print()
    if unassigned:
        print(f"UNASSIGNED ({len(unassigned)}):")
        for i in unassigned:
            print(f"  [{i['comp']}] {i['impact'][:120]}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
