# CBMC discharge survey across the base set (2026-06)

**Date:** 2026-06-11.  Cast the CBMC net wide: auto-discharge a diverse
sample of base-set HIGH count/index candidates (29 functions across 25 TUs
spanning acpi, gpio, hid, media, misc, mtd, net/{arcnet,wwan,atm,ax25,nfc,
sched}, pci, scsi, staging, usb, fs/{afs,ntfs3}) and categorise CBMC's
outcomes.  `cbmc_discharge_survey.py` goto-cc's each TU (front-end outcome)
and runs `cbmc --function <f> --bounds-check --pointer-check --object-bits 16
--unwind 6 --partial-loops` (discharge outcome); resumable.

## Front-end (goto-cc): 29/29 OK

Every TU built -- no parse/conversion gaps in this diverse sample.  The
`__seg_fs`/`__seg_gs` fix plus the earlier front-end work generalise across
subsystems; the C front-end is NOT the bottleneck for kernel discharge.

## A survey-discovered CBMC adjustment: `--object-bits`

12 candidates first failed with *"too many addressed objects: maximum 2^8=256;
use --object-bits n"* (returncode 6).  Whole-function analysis with nondet
input pointers over the full call graph allocates >256 distinct objects,
exceeding CBMC's default `object-bits=8`.  Setting `--object-bits 16`
eliminates this failure mode entirely (the 12 reclassified to real verdicts).
This is a required flag for whole-function kernel discharge.

## Discharge outcome distribution (29 candidates, 120 s, object-bits 16)

| outcome | count | meaning |
|---------|------:|---------|
| TIMEOUT | 18 | whole-function symex/solver does not finish in 120 s |
| VIOLATED | 10 | a bounds/pointer violation is reachable under UNCONSTRAINED inputs -- a real OOB *shape*; mostly the non-local-validator class (cf. mqprio/cec): needs a caller-precondition to separate genuine bugs from validator-guarded FPs |
| PROVED | 1 | CBMC proves the function safe whole-body even with nondet inputs -- robustly safe |

* **PROVED (CBMC works fine):** `supinfo_to_lineinfo` (gpiolib-cdev.c) --
  a clean, definitive whole-function clear with no harness or precondition.
* **VIOLATED (CBMC works fine, produces a counterexample):**
  logi_dj_hidpp_event, hiddev_lookup_report, pulse8_interrupt,
  pulse8_irq_work_handler, ipc_mux_ul_adgh_finish, ioctl_event_ctl,
  vchiq_compat_ioctl_queue_message, afs_deliver_vl_get_entry_by_name_u,
  nfc_hci_cmd_received, fl_set_key_mpls_lse.  These are the triage frontier:
  CBMC found the OOB shape; each needs its non-local precondition modelled
  (as for mqprio/cec) to decide genuine-vs-guarded.
* **TIMEOUT:** the dominant failure mode -- whole-function symbolic execution
  on real kernel functions does not scale at 120 s (nondet-pointer reasoning
  over the full call graph, big inlined callees).  These need the
  verbatim-slice / caller-precondition harness, or fn-ptr/callee stubbing
  (fp_restrict.py), or a larger time budget.

## Takeaways

1. The **front-end is solved** for this surface (100 % build); the bottleneck
   is whole-function symex scaling.
2. `--object-bits 16` is arequired flag (eliminated 12/29 spurious failures).
3. CBMC *does* work fine on a meaningful fraction: **11/29 (38 %)** reach a
   definitive verdict (1 PROVED + 10 VIOLATED) whole-function; the other
   62 % time out and need slicing/preconditioning.
4. The 10 VIOLATED are concrete triage targets (real OOB shapes); the 1
   PROVED is a clean clear.  Neither came from a heuristic -- only from CBMC.

## Update: triage of VIOLATED + higher timeout (efforts 1+2)

**Triage finding -- whole-function VIOLATED verdicts are largely
nondet-INPUT artifacts, not bugs.**  Extracting the failing properties:

* with `--pointer-check`, 9/10 VIOLATED failed on a nondet-INPUT pointer
  deref (`dev->driver_data`, `file->private_data`, `mpls->used_lses`,
  `ul_adb->dest_skb` ... all NULL/invalid because the input struct pointer
  is unconstrained), NOT the candidate array;
* with `--bounds-check` only, they still FAIL -- but the decisive case is
  `nfc_hci_cmd_received`, which is LOCALLY guarded
  (`if (pipe >= NFC_HCI_MAX_PIPES) goto exit;` before `hdev->pipes[pipe]`)
  yet reports `array.pipes dynamic object upper bound: FAILURE`.  Reason:
  under `--function`, `hdev` is a nondet pointer, so `hdev->pipes` is a
  "dynamic object" of unknown size and the bounds check fails spuriously
  even for safe code.

So raw whole-function `--function` discharge with nondet input pointers is
**unsuitable for adjudicating these candidates**: it manufactures spurious
bounds/pointer failures from unconstrained inputs.  A VIOLATED here means
"real OOB shape *under unconstrained input*", which is the non-local-
validator class (cf. mqprio/cec) -- NOT a confirmed bug.  Meaningful
adjudication needs a harness that allocates inputs with CONCRETE sizes (the
verbatim-slice / caller-precondition approach) -- so that approach is
*necessary*, not merely a tractability convenience.  The survey now runs
bounds-check ONLY (pointer-check removed) to cut the worst of the noise.

**Higher timeout (120s -> 300s):** TIMEOUT 18 -> 14 (4 resolved, all to
VIOLATED), +1 OOM (csio_mb_fwevt_handler, hit the 48 GB cap).  More time
mostly converts timeouts into (artifact-prone) VIOLATED, not into clean
PROVED -- diminishing returns, confirming the bottleneck is the methodology
(nondet whole-function inputs), not just the budget.

Refined distribution (bounds-only, 300 s, object-bits 16): TIMEOUT 14,
VIOLATED 12, OOM 1, PROVED 1.  Net: only `supinfo_to_lineinfo` is a clean
whole-function clear; the rest are timeouts or nondet-input artifacts.  The
actionable conclusion: route candidates through harnessed slices (concrete
input sizes) rather than raw `--function`.

## Update: widening to skb/cursor + A2 candidate types (effort 3)

Added a class-diverse sample of skb/cursor + A2 (infoleak) candidates
(bfusb_rx_submit, hfcmulti_rx, at91_start_xmit,
fdp_nci_core_get_config_rsp_packet, br2684_push, ax25_ioctl) via
`cbmc_discharge_survey.py` + an `extra.json` candidate file (extracted by
running skb_field_before_lencheck / infoleak on cached leaf DBs).  All built
OK (front-end again 100%); discharge: 5 TIMEOUT + 1 VIOLATED -- the SAME
distribution as the count/index set.  The failure-mode profile is therefore
candidate-type-independent: front-end solid, whole-function symex dominated
by timeout and nondet-input artifacts.

## Combined survey conclusions (35 candidates, all classes + asset types)

* **Front-end: 100% build** -- not the bottleneck anywhere.
* **`--object-bits 16` required** (eliminated 12 spurious "too many objects").
* **Whole-function raw `--function` discharge is the wrong tool** for these:
  nondet input pointers => spurious pointer/bounds artifacts (even locally
  guarded code fails), and >~half time out.  Only robustly-safe leaves
  (supinfo_to_lineinfo) clear cleanly.
* **The path that works** is the harnessed verbatim slice / caller-
  precondition (concrete input sizes), as already demonstrated for
  cec / mqprio / taprio / altera -- those gave definitive verdicts where the
  raw whole-function run only times out or manufactures artifacts.

Net: the survey's value is the *map* -- it cheaply sorts candidates into
"clean PROVED" (rare), "needs a harness" (the VIOLATED/TIMEOUT bulk), and
confirms the front-end + object-bits prerequisites -- so effort focuses on
harnessing the genuine-shape candidates, not on raw whole-function runs.
