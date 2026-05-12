#!/usr/bin/env bash
#
# corpus-scan.sh — run the proactive scan against a fixed corpus of
# Linux kernel files that the aead and pipe_buffer Coccinelle
# prefilters are expected to hit.
#
# Intended for overnight or nightly CI use.  For each file, runs
# scan.py with default `--direction=vuln` (the bug-finding
# direction) and records:
#
#   * cocci prefilter hit counts per module
#   * cbmc_status (failed / successful / timeout / error / vacuity-risk)
#   * for failed verdicts, the contract-precondition assertion name
#
# Output: a compact text summary plus the raw per-file JSON under
# $OUTDIR.
#
# Usage:
#   LINUX_TREE=/path/to/linux-5.10 ./corpus-scan.sh [outdir]
#
# Budget: ~100s per file under the default CBMC timeout.  The
# corpus below has 19 files → ~30 minutes wall-clock with no
# parallelism.  Set PARALLEL=N to fan out.

set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/_lib.sh"

: "${LINUX_TREE:?LINUX_TREE must point at a Linux source tree}"
OUTDIR=${1:-/tmp/corpus-scan-$(date +%Y%m%d-%H%M%S)}
PARALLEL=${PARALLEL:-1}

mkdir -p "$OUTDIR"

# Curated corpus.  Files picked by `grep -l` for the primary kernel
# API each property module targets:
#   aead        — `aead_request_set_crypt(` call sites
#   pipe_buffer — `struct pipe_buffer` references
#
# Intentionally mixes kernel subsystems that are easy to compile
# (crypto/, fs/) with a few driver and trace files that stress
# the compile_file.sh -I soup.  Files that fail to compile under
# our kernel-config setup are reported honestly (cbmc_status:
# error) rather than hidden.
CORPUS=(
  # aead corpus
  crypto/algif_aead.c
  crypto/ccm.c
  crypto/echainiv.c
  crypto/essiv.c
  crypto/gcm.c
  crypto/pcrypt.c
  crypto/seqiv.c
  crypto/tcrypt.c
  crypto/testmgr.c

  # pipe_buffer corpus
  fs/splice.c
  fs/pipe.c
  lib/iov_iter.c
  fs/fuse/dev.c
  fs/nfsd/vfs.c
  net/smc/smc_rx.c
  kernel/trace/trace.c
  kernel/relay.c
  kernel/watch_queue.c
)

echo "corpus: ${#CORPUS[@]} files; out dir = $OUTDIR"
echo

scan_one() {
  local rel=$1
  local src="$LINUX_TREE/$rel"
  local stem
  stem=$(echo "$rel" | tr '/' '_')
  local json="$OUTDIR/$stem.json"
  local log="$OUTDIR/$stem.log"

  if [[ ! -f $src ]]; then
    echo "skip (missing): $rel" >&2
    return
  fi

  LINUX_TREE="$LINUX_TREE" "$SCRIPT_DIR/scan.py" "$src" \
    --json "$json" > "$log" 2>&1 || true
}

# Run.  Simple sequential loop for now; PARALLEL=N would require
# an xargs-or-gnu-parallel wrapper, left for later.
i=0
for rel in "${CORPUS[@]}"; do
  i=$((i + 1))
  echo "[$i/${#CORPUS[@]}] $rel"
  scan_one "$rel"
done

echo
echo "=== summary ==="
python3 - "$OUTDIR" <<'PY'
import json
import os
import sys

outdir = sys.argv[1]

# Per-row structure: (file, module, cocci_hits, cbmc_status, assertion, notes)
rows = []
for name in sorted(os.listdir(outdir)):
    if not name.endswith(".json"):
        continue
    with open(os.path.join(outdir, name)) as f:
        d = json.load(f)
    for file in d["files"]:
        path = file["file"]
        for m in file["modules"]:
            hits = len(m.get("cocci_hits") or [])
            status = m.get("cbmc_status")
            if hits == 0 and status in ("not-run", None):
                continue
            assertion = ""
            if m.get("cbmc_failures"):
                assertion = m["cbmc_failures"][0].get("assertion", "")
            notes = m.get("cbmc_notes", "") or ""
            rows.append((path, m["module"], hits, status, assertion, notes))

# Categorise.  Post-LIM-012, the direct-call harness fires the
# same contract against the same synthetic vulnerable shape on
# every scan.  A file-level "failed" verdict therefore does NOT
# mean "this file has the Dirty Pipe / Copy Fail bug"; it means
# "this file compiled and linked cleanly, and the property
# module's contract catches the bug class on kernel-layout
# inputs".  The per-file signal is the Coccinelle prefilter hit
# list, not the cbmc_status.
pipeline_ok = [r for r in rows if r[3] == "failed"]
compile_fail = [r for r in rows if r[3] == "error"]
other = [r for r in rows
         if r[3] not in ("failed", "error", "not-run", None)]

if pipeline_ok:
    print(f"\n--- pipeline-ok: {len(pipeline_ok)} file(s) — "
          f"compile + link + contract works as expected ---")
    print("  (cbmc_status='failed' because the direct-call harness "
          "is the same across files;")
    print("   per-file signal is the cocci hit list below.)")
    width = max(len(r[0]) for r in pipeline_ok) + 2
    for path, module, hits, _, assertion, _ in pipeline_ok:
        print(f"  {path:<{width}} {module:<12}  hits={hits:<3}  "
              f"assertion={assertion}")

if compile_fail:
    print(f"\n--- compile-fail: {len(compile_fail)} file(s) — "
          f"goto-cc could not build the kernel TU ---")
    print("  (typically missing kernel config flags for the subsystem;")
    print("   see scan/configure.sh fragments.)")
    width = max(len(r[0]) for r in compile_fail) + 2
    for path, module, hits, _, _, notes in compile_fail:
        first_err = ""
        for line in notes.splitlines():
            if "error:" in line:
                first_err = line.strip()
                break
        print(f"  {path:<{width}} {module:<12}  hits={hits:<3}  "
              f"{first_err[:80]}")

if other:
    print(f"\n--- other ({len(other)}) ---")
    for path, module, hits, status, assertion, _ in other:
        print(f"  {path}  {module}  hits={hits}  status={status}  "
              f"{assertion}")

print()
print("counts:")
print(f"  files scanned              : {len({r[0] for r in rows})}")
print(f"  pipeline-ok                : {len(pipeline_ok)}")
print(f"  compile-fail               : {len(compile_fail)}")
print(f"  other (review)             : {len(other)}")
total_hits = sum(r[2] for r in rows)
print(f"  total cocci prefilter hits : {total_hits}")
PY
