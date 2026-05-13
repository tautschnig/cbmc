#!/usr/bin/env bash
#
# precision-characterisation.sh — measure per-kernel precision of
# the proactive scan across the four LTS kernel trees we support.
#
# For each property module, runs the scan (in both adapter mode
# and --per-file mode where supported) against one anchor file
# that is known to exercise the module's bug class, across Linux
# 5.10, 6.1, 6.6, and 6.12.  Emits a Markdown table showing:
#
#   - prefilter hit count per (kernel, module),
#   - adapter-mode cbmc_status per (kernel, module),
#   - --per-file verdict counts per (kernel, module).
#
# The goal is to validate two precision claims:
#
#   1. The module's prefilter / harness signal is stable across
#      kernels (same call-site pattern gets the same verdict
#      shape).
#
#   2. Where upstream has REMOVED the vulnerable pattern (e.g.
#      copy_page_to_iter_pipe removed in 6.6+), the scan
#      honestly reports zero hits, not a false-positive.
#
# Usage:
#
#   ./precision-characterisation.sh [OUTDIR]
#
# Output:
#   OUTDIR/precision-characterisation.md  — summary table
#   OUTDIR/<kernel>_<module>.json         — per-run scan output

set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
OUTDIR=${1:-/tmp/precision-characterisation-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUTDIR"

# (kernel_tree_dir, kernel_version, module, anchor_file, per_file_supported)
# per_file_supported: "yes" or "no" (no = skip --per-file for this row;
# currently aead is adapter-only by design).
CASES=(
  '5_10:5.10:aead:crypto/algif_aead.c:no'
  '6_1:6.1:aead:crypto/algif_aead.c:no'
  '6_6:6.6:aead:crypto/algif_aead.c:no'
  '6_12:6.12:aead:crypto/algif_aead.c:no'

  '5_10:5.10:pipe_buffer:lib/iov_iter.c:yes'
  '6_1:6.1:pipe_buffer:lib/iov_iter.c:yes'
  '6_6:6.6:pipe_buffer:lib/iov_iter.c:yes'
  '6_12:6.12:pipe_buffer:lib/iov_iter.c:yes'

  '5_10:5.10:cred_lifetime:fs/coredump.c:yes'
  '6_1:6.1:cred_lifetime:fs/coredump.c:yes'
  '6_6:6.6:cred_lifetime:fs/coredump.c:yes'
  '6_12:6.12:cred_lifetime:fs/coredump.c:yes'

  '5_10:5.10:lock_state:kernel/bpf/dispatcher.c:yes'
  '6_1:6.1:lock_state:kernel/bpf/dispatcher.c:yes'
  '6_6:6.6:lock_state:kernel/bpf/dispatcher.c:yes'
  '6_12:6.12:lock_state:kernel/bpf/dispatcher.c:yes'

  '5_10:5.10:refcount_lifetime:kernel/fork.c:yes'
  '6_1:6.1:refcount_lifetime:kernel/fork.c:yes'
  '6_6:6.6:refcount_lifetime:kernel/fork.c:yes'
  '6_12:6.12:refcount_lifetime:kernel/fork.c:yes'
)

SCAN="$SCRIPT_DIR/scan.py"
REPORT="$OUTDIR/precision-characterisation.md"

{
  echo "# Precision characterisation"
  echo
  echo "Generated $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
  echo
  echo "For each (kernel × module × anchor file) triple, reports"
  echo "(a) the Coccinelle prefilter hit count, (b) the adapter-"
  echo "mode \`cbmc_status\`, and (c) the \`--per-file\` verdict"
  echo "counts (failed / successful / timeout / error / other)."
  echo "A stable pattern across kernels is the scan doing its job;"
  echo "a shift from \`hits=N\` to \`hits=0\` at a specific version"
  echo "reflects upstream having removed the vulnerable pattern."
  echo
  echo "## Results"
  echo
  printf '| kernel | module | anchor | hits | adapter | per-file (f/s/t/e) |\n'
  printf '|--------|--------|--------|------|---------|---------------------|\n'
} > "$REPORT"

for row in "${CASES[@]}"; do
  IFS=':' read -r tree_dir kver module anchor pf_supported <<< "$row"
  tree="$HOME/linux_$tree_dir"
  if [[ ! -d "$tree" ]]; then
    printf '| %s | %s | %s | skipped (no tree) | — | — |\n' \
      "$kver" "$module" "$anchor" >> "$REPORT"
    continue
  fi
  anchor_path="$tree/$anchor"
  if [[ ! -f "$anchor_path" ]]; then
    printf '| %s | %s | %s | absent | — | — |\n' \
      "$kver" "$module" "$anchor" >> "$REPORT"
    continue
  fi

  stem="$OUTDIR/${tree_dir}_${module}"
  adapter_json="$stem.adapter.json"
  adapter_log="$stem.adapter.log"

  # Adapter-mode run.
  timeout 900 env LINUX_TREE="$tree" \
    "$SCAN" "$anchor_path" \
    --json "$adapter_json" > "$adapter_log" 2>&1 || true

  # Extract per-module prefilter hit count and cbmc_status for
  # the row's module.
  read -r hits adapter_status <<< "$(
    python3 - "$adapter_json" "$module" <<'PY'
import json, sys
path, module = sys.argv[1], sys.argv[2]
try:
    d = json.load(open(path))
except (OSError, json.JSONDecodeError):
    print("0 error")
    sys.exit(0)
for f in d.get("files", []):
    for m in f.get("modules", []):
        if m["module"] != module:
            continue
        print(len(m.get("cocci_hits") or []),
              m.get("cbmc_status", "unknown"))
        sys.exit(0)
print("0 not-found")
PY
  )"

  # --per-file run, if supported.
  pf_failed=0; pf_successful=0; pf_timeout=0; pf_error=0
  if [[ "$pf_supported" == "yes" ]]; then
    pf_json="$stem.perfile.json"
    pf_log="$stem.perfile.log"
    timeout 1800 env LINUX_TREE="$tree" UNWIND=3 \
      "$SCAN" --per-file "$anchor_path" \
      --json "$pf_json" > "$pf_log" 2>&1 || true
    read -r pf_failed pf_successful pf_timeout pf_error <<< "$(
      python3 - "$pf_json" "$module" <<'PY'
import json, sys
path, module = sys.argv[1], sys.argv[2]
try:
    d = json.load(open(path))
except (OSError, json.JSONDecodeError):
    print("0 0 0 0")
    sys.exit(0)
f = s = t = e = 0
for file in d.get("files", []):
    for m in file.get("modules", []):
        if m["module"] != module:
            continue
        for v in (m.get("per_file") or []):
            st = v.get("status")
            if st == "failed":      f += 1
            elif st == "successful": s += 1
            elif st == "timeout":   t += 1
            else:                   e += 1
print(f, s, t, e)
PY
    )"
    pf_cell="${pf_failed}/${pf_successful}/${pf_timeout}/${pf_error}"
  else
    pf_cell="n/a"
  fi

  printf '| %s | %s | %s | %s | %s | %s |\n' \
    "$kver" "$module" "$anchor" "$hits" "$adapter_status" "$pf_cell" \
    >> "$REPORT"
done

{
  echo
  echo "## Notes"
  echo
  echo "- \`hits=0\` at a particular version means the Coccinelle"
  echo "  prefilter found no call sites matching the module's"
  echo "  seed pattern.  For \`pipe_buffer\` on \`lib/iov_iter.c\`,"
  echo "  this is the expected shape on 6.6+ because upstream"
  echo "  removed \`copy_page_to_iter_pipe\` entirely."
  echo "- adapter-mode \`failed\` is a property-module self-check"
  echo "  (the direct-call harness's vulnerable synthetic shape"
  echo "  fires the contract).  It is NOT a per-file bug signal;"
  echo "  see \`CBMC_LIMITATIONS.md\` LIM-013."
  echo "- per-file counts are \`failed / successful / timeout /"
  echo "  error\`.  \`failed\` means the synthesised per-enclosing-"
  echo "  function harness triggered the contract; \`successful\`"
  echo "  means it didn't; \`timeout\` means the symex didn't"
  echo "  terminate inside the per-file budget."
  echo "- \`aead\` per-file is deliberately \`n/a\` — the predicate"
  echo "  walks \`req->dst\`'s scatterlist, which cannot be"
  echo "  fabricated from a single \`aead_request *\` parameter."
} >> "$REPORT"

echo "Precision report: $REPORT"
cat "$REPORT"
