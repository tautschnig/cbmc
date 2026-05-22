#!/usr/bin/env bash
#
# corpus-scan.sh — run the proactive scan against a discovered
# corpus of Linux kernel files that one of our Coccinelle
# prefilters is likely to hit.
#
# ## What it does
#
# 1. Auto-discovers candidate kernel files under $LINUX_TREE by
#    grepping for per-module "textual seed" patterns (see
#    CORPUS_PATTERNS below).  Each property module contributes
#    one row: module name + a regex passed to `grep -rlE`.
#
# 2. Dedupes and caps the total corpus (default 200 files; set
#    CORPUS_MAX to override).  A shorter curated fallback is used
#    when discovery is disabled via CORPUS_DISCOVERY=0.
#
# 3. For each file, runs scan.py with default `--direction=vuln`
#    and records the structured JSON + log.  Fan-out is
#    controlled by PARALLEL (default: $(nproc)/2, at least 1).
#
# 4. Aggregates the per-file JSON into a categorised summary
#    (pipeline-ok / compile-fail / vacuity-risk / timeout / etc.)
#    plus total prefilter-hit count.
#
# Post-LIM-012 note: the direct-call harness fires the SAME
# contract against the SAME synthetic vulnerable shape on every
# file, so a "cbmc_status=failed" verdict is NOT a per-file bug
# signal.  The per-file signal is the Coccinelle prefilter hit
# list; the cbmc verdict is a property-module self-check.
#
# ## Usage
#
#   LINUX_TREE=/path/to/linux ./corpus-scan.sh [OUTDIR]
#
# ## Tuning
#
#   PARALLEL=N           fan-out (default nproc/2)
#   CORPUS_MAX=N         cap total files (default 200)
#   CORPUS_DISCOVERY=0   skip discovery; use the hand-curated
#                        18-file fallback (faster for smoke tests)
#   SCAN_MEMORY_LIMIT=N  passed through to scan.py's _run helper
#                        (per-tool RLIMIT_AS, default 24 GiB)
#   SCAN_CPU_LIMIT=N     per-tool RLIMIT_CPU in seconds (default 900)

set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)

: "${LINUX_TREE:?LINUX_TREE must point at a Linux source tree}"
OUTDIR=${1:-/tmp/corpus-scan-$(date +%Y%m%d-%H%M%S)}
PARALLEL=${PARALLEL:-$(( $(nproc) / 2 ))}
if (( PARALLEL < 1 )); then PARALLEL=1; fi
CORPUS_MAX=${CORPUS_MAX:-200}
CORPUS_DISCOVERY=${CORPUS_DISCOVERY:-1}

mkdir -p "$OUTDIR"

# Per-module textual seed patterns.  Each entry: 'module<TAB>regex<TAB>scopes'.
# Using TAB as delimiter so the regex is free to contain `|`.
# `regex` is passed to `grep -lE` as a single ERE.  `scopes` is a
# space-separated list of LINUX_TREE-relative directories to search.
# Keeping the scopes conservative avoids sweeping up drivers/* etc.
# which are large and rarely buildable under our allnoconfig setup.
CORPUS_PATTERNS=(
  # aead: call sites of aead_request_set_crypt.
  $'aead\taead_request_set_crypt\\(\tcrypto'
  # pipe_buffer: references to struct pipe_buffer.
  $'pipe_buffer\tstruct pipe_buffer\tfs lib net/smc'
  # cred_lifetime: put_cred or __put_cred call sites.
  $'cred_lifetime\t(^|[^a-zA-Z0-9_])(__)?put_cred\\(\tfs kernel net ipc security'
  # Phase-1 balance modules.
  $'device_lifetime\t(^|[^a-zA-Z0-9_])put_device\\(\tdrivers/base drivers/of fs net'
  $'of_node_lifetime\tof_node_put\\(\tdrivers/of drivers/base'
  $'inode_lifetime\t(^|[^a-zA-Z0-9_])iput\\(\tfs'
  $'dentry_lifetime\t(^|[^a-zA-Z0-9_])dput\\(\tfs'
  $'fput_lifetime\t(^|[^a-zA-Z0-9_])fput\\(\tfs ipc kernel'
  $'sock_lifetime\t(^|[^a-zA-Z0-9_])sock_put\\(\tnet ipc'
  $'skb_lifetime\tkfree_skb\\(\tnet drivers/net'
  $'module_lifetime\tmodule_put\\(\tkernel fs net drivers/base'
  # Phase-2 modules.
  $'kobject_lifetime\tkobject_put\\(\tlib drivers/base block'
  $'kref_lifetime\tkref_put\\(\tfs net drivers/base kernel'
  $'rcu_critical_section\tsynchronize_rcu\\(\\)\tnet kernel fs ipc'
  $'cancel_work_before_free\tINIT_WORK\\(\tdrivers/base fs net kernel'
  $'cancel_delayed_work_before_free\tINIT_DELAYED_WORK\\(\tdrivers/base fs net kernel'
  $'del_timer_sync_before_free\ttimer_setup\\(\tdrivers/base fs net kernel'
  # Beyond-50% additions: very broad scope (lots of cocci hits
  # expected; primary integration is hand triage).
  $'null_after_alloc\tkmalloc\\(\tdrivers/base fs net kernel ipc lib mm'
  $'resource_leak_on_error_path\tkmalloc\\(\tdrivers/base fs net kernel ipc'
  $'integer_overflow_in_alloc_size\tkmalloc\\(.*\\*\\s*sizeof\tdrivers/base fs net kernel'
  $'copy_from_user_size_check\tcopy_from_user\\(\tdrivers/base fs net kernel ipc'
  $'use_after_free_generic\tkfree\\(\tdrivers/base fs net kernel ipc lib mm'
  $'division_by_zero_check\tdo_div\\(|div_u64\\(|div_s64\\(\tdrivers/base fs net kernel'
  $'uninit_to_user\tcopy_to_user\\(.*sizeof\\(\tdrivers/base fs net kernel ipc'
  # Existing modules without patterns yet.
  $'lock_state\tmutex_unlock\\(\tkernel fs net ipc'
  $'refcount_lifetime\trefcount_dec_and_test\\(\tkernel fs net'
  $'alloc_tag\tvfree\\(\tkernel fs mm'
)

# Hand-curated fallback, used when CORPUS_DISCOVERY=0.  Matches the
# earlier hard-coded list; useful as a smoke test.
FALLBACK_CORPUS=(
  crypto/algif_aead.c crypto/ccm.c crypto/echainiv.c crypto/essiv.c
  crypto/gcm.c crypto/pcrypt.c crypto/seqiv.c crypto/tcrypt.c
  crypto/testmgr.c
  fs/splice.c fs/pipe.c lib/iov_iter.c fs/fuse/dev.c fs/nfsd/vfs.c
  net/smc/smc_rx.c kernel/trace/trace.c kernel/relay.c
  kernel/watch_queue.c
  fs/coredump.c fs/io_uring.c
)

discover_corpus() {
  local -a accum=()
  local seen_tmp
  seen_tmp=$(mktemp)
  trap 'rm -f "$seen_tmp"' RETURN

  for row in "${CORPUS_PATTERNS[@]}"; do
    local module regex scopes
    IFS=$'\t' read -r module regex scopes <<< "$row"
    local -a scopes_arr
    # shellcheck disable=SC2206
    scopes_arr=($scopes)
    local -a existing=()
    for s in "${scopes_arr[@]}"; do
      if [[ -d "$LINUX_TREE/$s" ]]; then
        existing+=("$s")
      fi
    done
    if (( ${#existing[@]} == 0 )); then
      continue
    fi
    # grep -rlE, restricted to .c files, strip LINUX_TREE prefix.
    while IFS= read -r path; do
      path=${path#"$LINUX_TREE"/}
      if ! grep -qxF "$path" "$seen_tmp"; then
        echo "$path" >> "$seen_tmp"
        accum+=("$path")
      fi
    done < <(
      cd "$LINUX_TREE" \
        && grep -rlE --include='*.c' "$regex" "${existing[@]}" 2>/dev/null \
        | sort
    )
  done

  # Cap.
  local -a capped=()
  local i=0
  for path in "${accum[@]}"; do
    if (( i >= CORPUS_MAX )); then break; fi
    capped+=("$path")
    i=$((i + 1))
  done
  printf '%s\n' "${capped[@]}"
}

if (( CORPUS_DISCOVERY == 1 )); then
  echo "Discovering corpus (grep -rlE across module patterns)..." >&2
  mapfile -t CORPUS < <(discover_corpus)
else
  CORPUS=("${FALLBACK_CORPUS[@]}")
fi

echo "corpus: ${#CORPUS[@]} files; out dir = $OUTDIR; parallelism = $PARALLEL"
echo

# Per-file worker.  Reads one path per stdin line, writes JSON.
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

  # The scan wraps its own tool invocations in
  # _rlimit_preexec (RLIMIT_AS + RLIMIT_CPU); we just need a
  # wall-clock outer backstop so a dead subprocess cannot
  # hang the corpus run.  Budget defaults to 900s per file —
  # the aead scan under vacuity probe + verify is the slowest
  # at ~200s in default mode, giving headroom.  Per-file mode
  # synthesises a harness per cocci hit so the total per-file
  # wall-clock can be much higher; SCAN_FILE_TIMEOUT lets the
  # caller raise the cap (default 900s for default-mode runs,
  # 7200s when EXTRA_SCAN_ARGS contains --per-file).
  local file_to=${SCAN_FILE_TIMEOUT:-}
  if [[ -z $file_to ]]; then
    if [[ ${EXTRA_SCAN_ARGS:-} == *--per-file* ]]; then
      file_to=7200
    else
      file_to=900
    fi
  fi
  # shellcheck disable=SC2086
  timeout "$file_to" "$SCRIPT_DIR/scan.py" "$src" \
    --json "$json" \
    ${EXTRA_SCAN_ARGS:-} \
    > "$log" 2>&1 || true
}

export -f scan_one
export SCRIPT_DIR LINUX_TREE OUTDIR EXTRA_SCAN_ARGS SCAN_FILE_TIMEOUT

# Fan-out.  xargs -P keeps the machine loaded without needing GNU
# parallel.  `bash -c` wrapper needed because xargs can't call
# exported shell functions directly.
start=$(date +%s)
printf '%s\n' "${CORPUS[@]}" | \
  xargs -I {} -P "$PARALLEL" bash -c 'scan_one "$@"' _ {}
end=$(date +%s)
echo
echo "corpus scan wall-clock: $((end - start))s"

echo
echo "=== summary ==="
python3 - "$OUTDIR" <<'PY'
import json
import os
import sys

outdir = sys.argv[1]

# Two parallel row tables:
#   default-mode rows      — one per (file, module) with the
#                            module's overall cbmc_status.
#   per-file-mode rows     — one per (file, module, function)
#                            with the per-call-site verdict.
#
# A run can produce either or both: --per-file scans still set
# the module's cbmc_status to a roll-up of the per-call-site
# verdicts, but the per-call-site list is what we want to triage.
default_rows = []   # (file, module, hits, status, assertion, notes)
perfile_rows = []   # (file, module, function, hit_lines, status, notes)

for name in sorted(os.listdir(outdir)):
    if not name.endswith(".json"):
        continue
    with open(os.path.join(outdir, name)) as f:
        try:
            d = json.load(f)
        except json.JSONDecodeError:
            # scan.py crashed or was killed; surface via the log
            continue
    for file in d["files"]:
        path = file["file"]
        for m in file["modules"]:
            hits = len(m.get("cocci_hits") or [])
            status = m.get("cbmc_status")
            per_file = m.get("per_file") or []
            if hits == 0 and status in ("not-run", None) and not per_file:
                continue
            assertion = ""
            if m.get("cbmc_failures"):
                assertion = m["cbmc_failures"][0].get("assertion", "")
            notes = m.get("cbmc_notes", "") or ""
            default_rows.append(
                (path, m["module"], hits, status, assertion, notes)
            )
            for pf in per_file:
                perfile_rows.append((
                    path,
                    m["module"],
                    pf.get("function", "?"),
                    pf.get("hit_lines") or [],
                    pf.get("status", "?"),
                    pf.get("notes", "") or "",
                ))

# === default-mode summary (file-level cbmc_status) =====================
groups = {
    "pipeline-ok": [],   # cbmc_status=failed (contract fires as expected)
    "compile-fail": [],
    "timeout": [],
    "vacuity-risk": [],
    "other": [],
}
for r in default_rows:
    status = r[3]
    if status == "failed":
        groups["pipeline-ok"].append(r)
    elif status == "error":
        groups["compile-fail"].append(r)
    elif status == "timeout":
        groups["timeout"].append(r)
    elif status == "vacuity-risk":
        groups["vacuity-risk"].append(r)
    else:
        groups["other"].append(r)

def _width(rs, idx=0):
    return max((len(str(r[idx])) for r in rs), default=1) + 2

if groups["pipeline-ok"]:
    rs = groups["pipeline-ok"]
    print(f"\n--- pipeline-ok: {len(rs)} row(s) — "
          f"compile + link + contract fires (LIM-013: in default mode "
          f"this is a self-check, NOT a per-file bug signal) ---")
    w = _width(rs)
    for path, module, hits, _, assertion, _ in rs:
        print(f"  {path:<{w}} {module:<14}  hits={hits:<3}  "
              f"{assertion}")

for cat in ("compile-fail", "timeout", "vacuity-risk"):
    rs = groups[cat]
    if not rs:
        continue
    print(f"\n--- {cat}: {len(rs)} row(s) ---")
    w = _width(rs)
    for path, module, hits, _, _, notes in rs:
        first_err = ""
        for line in notes.splitlines():
            if "error:" in line or "exit 1" in line:
                first_err = line.strip()
                break
        print(f"  {path:<{w}} {module:<14}  hits={hits:<3}  "
              f"{first_err[:80]}")

if groups["other"]:
    print(f"\n--- other: {len(groups['other'])} row(s) ---")
    for path, module, hits, status, assertion, _ in groups["other"]:
        print(f"  {path}  {module}  hits={hits}  status={status}  "
              f"{assertion}")

# === per-file-mode summary (one row per call site) ====================
if perfile_rows:
    pf_groups = {
        "failed": [],         # candidate bugs — needs triage
        "successful": [],     # contract holds at this site
        "noise": [],          # only CBMC built-in checks fired
        "vacuous": [],        # no contract clause was checked
        "skipped": [],        # known-unverifiable shape
        "timeout": [],
        "error": [],          # synthesis / link / compile error
        "no-function-found": [],
        "other": [],
    }
    for r in perfile_rows:
        s = r[4]
        if s in pf_groups:
            pf_groups[s].append(r)
        else:
            pf_groups["other"].append(r)

    print()
    print("=" * 70)
    print(f"PER-FILE MODE: {len(perfile_rows)} per-call-site verdict(s) "
          f"across {len({(r[0], r[1]) for r in perfile_rows})} "
          f"(file, module) pair(s)")
    print("=" * 70)

    if pf_groups["failed"]:
        rs = pf_groups["failed"]
        print(f"\n--- per-file FAILED (candidate bug sites): "
              f"{len(rs)} row(s) ---")
        w_path = _width(rs, 0)
        w_func = _width(rs, 2)
        for path, module, func, hit_lines, _, _ in rs:
            lines = ",".join(str(L) for L in hit_lines)
            print(f"  {path:<{w_path}} {module:<14}  "
                  f"{func:<{w_func}}  hits@L:{lines}")

    for cat in ("timeout", "error", "no-function-found", "other"):
        rs = pf_groups[cat]
        if not rs:
            continue
        print(f"\n--- per-file {cat}: {len(rs)} row(s) ---")
        w_path = _width(rs, 0)
        w_func = _width(rs, 2)
        for path, module, func, hit_lines, _, notes in rs:
            lines = ",".join(str(L) for L in hit_lines)
            first_err = ""
            for line in (notes or "").splitlines():
                if "error:" in line or "exit 1" in line:
                    first_err = line.strip()
                    break
            print(f"  {path:<{w_path}} {module:<14}  "
                  f"{func:<{w_func}}  hits@L:{lines}  "
                  f"{first_err[:60]}")

    if pf_groups["successful"]:
        # Don't print the full list — it's noise.  Just count.
        print(f"\n--- per-file successful (contract holds): "
              f"{len(pf_groups['successful'])} row(s) ---")
    if pf_groups["noise"]:
        print(f"\n--- per-file noise (CBMC built-ins fired, "
              f"no contract violation): "
              f"{len(pf_groups['noise'])} row(s) ---")
    if pf_groups["vacuous"]:
        print(f"\n--- per-file vacuous (no contract clauses "
              f"checked): {len(pf_groups['vacuous'])} row(s) ---")
    if pf_groups.get("skipped"):
        print(f"\n--- per-file skipped (known-unverifiable "
              f"shape, e.g. aead transform-wrapper): "
              f"{len(pf_groups['skipped'])} row(s) ---")

# === counts ===========================================================
total_hits = sum(r[2] for r in default_rows)
print()
print("counts:")
print(f"  default-mode rows     : {len(default_rows)}")
print(f"  distinct files        : "
      f"{len({r[0] for r in default_rows})}")
for cat in ("pipeline-ok", "compile-fail", "timeout",
            "vacuity-risk", "other"):
    if groups[cat]:
        print(f"  {cat:<22}: {len(groups[cat])}")
print(f"  total cocci hits      : {total_hits}")
if perfile_rows:
    print(f"  per-file rows         : {len(perfile_rows)}")
    for cat in ("failed", "successful", "noise", "vacuous",
                "skipped",
                "timeout", "error", "no-function-found", "other"):
        if pf_groups.get(cat):
            print(f"  per-file {cat:<14}: {len(pf_groups[cat])}")
PY
