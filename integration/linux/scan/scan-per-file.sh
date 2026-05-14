#!/usr/bin/env bash
#
# scan-per-file.sh — run a per-file property-module scan on a
# single kernel source file + target function.
#
# Pipeline:
#   1. Compile the kernel TU with scan/compile_file.sh.
#   2. Generate a per-function harness via
#      scan/synthesise_harness.py (LIM-016 resolution path).
#   3. Compile the harness TU.
#   4. Link TU + harness + adapter + property module.
#   5. Apply `goto-instrument --replace-call-with-contract` on
#      the module's contract targets.
#   6. Apply `goto-instrument --drop-unused-functions` to drop
#      bodies of kernel functions trivially unreachable from
#      the synthesised main wrapper.  Essential for large
#      enclosing functions (e.g. do_coredump >1000 lines) so
#      cbmc's symex doesn't have to process unrelated siblings.
#   7. Run cbmc on the harness entry function.
#
# Exits 0 when cbmc reports VERIFICATION SUCCESSFUL, 10 when it
# reports VERIFICATION FAILED, other exit codes on infrastructure
# errors (unable to compile, link, synthesise, etc.).
#
# Usage:
#   LINUX_TREE=/path/to/linux ./scan-per-file.sh MODULE FILE FUNC
#
# Example:
#   LINUX_TREE=/home/ubuntu/linux_5_10 \
#     ./scan-per-file.sh cred_lifetime fs/nfsd/auth.c nfsd_setuser

set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/_lib.sh"

: "${LINUX_TREE:?LINUX_TREE must point at a Linux source tree}"

if [[ $# -lt 3 ]]; then
  echo "usage: $0 MODULE KERNEL-FILE TARGET-FUNCTION [contract-target...]" >&2
  exit 2
fi

MODULE=$1
KERNEL_FILE=$2
TARGET_FUNC=$3
shift 3
CONTRACT_TARGETS=("$@")

REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../.." &>/dev/null && pwd)

# Per-module naming: scan/adapters/<module-adapter-name>_kernel_adapter.c.
# Most modules use their own name; cred_lifetime and refcount_lifetime
# use shorter adapter stems by historical convention.  Resolve
# accordingly.
case "$MODULE" in
  cred_lifetime)     ADAPTER_STEM=cred ;;
  refcount_lifetime) ADAPTER_STEM=refcount ;;
  *)                 ADAPTER_STEM=$MODULE ;;
esac
ADAPTER="$SCRIPT_DIR/adapters/${ADAPTER_STEM}_kernel_adapter.c"
PROPERTY_SRC="$SCRIPT_DIR/../properties/${MODULE}/${MODULE}.c"

# aead is special: the property module's aead.c provides
# implementations of aead_request_set_crypt and friends with
# their own contract attached (via aead.h), and the contract
# disagrees with the kernel adapter on the assigns clause
# (the property module spells out req->src, req->dst, etc.;
# the adapter forward-declares struct aead_request as opaque
# and uses an empty assigns).  Linking both produces
# "conflict on code contract".  For per-file mode we only
# need the adapter's contract — the property module's
# implementation is unused on the kernel-call path — so
# clear PROPERTY_SRC for aead.  The aead adapter and harness
# also need page_provenance.c at link time for the
# `page_prov_of` and `set_page_prov` ghost-state backend; add
# it to the EXTRA_LINK_SRCS list.
EXTRA_LINK_SRCS=()
if [[ "$MODULE" == "aead" ]]; then
  PROPERTY_SRC=""
  EXTRA_LINK_SRCS+=("$SCRIPT_DIR/../properties/page_provenance/page_provenance.c")
fi

if [[ ! -f "$ADAPTER" ]]; then
  echo "no adapter for module '$MODULE' at $ADAPTER" >&2
  exit 2
fi
if [[ -n "$PROPERTY_SRC" && ! -f "$PROPERTY_SRC" ]]; then
  echo "no property source for module '$MODULE' at $PROPERTY_SRC" >&2
  exit 2
fi

# Default contract targets for well-known modules.
if [[ ${#CONTRACT_TARGETS[@]} -eq 0 ]]; then
  case "$MODULE" in
    cred_lifetime)
      CONTRACT_TARGETS=(
        __CPROVER_file_local_cred_h_put_cred
        put_cred
      )
      ;;
    pipe_buffer)
      CONTRACT_TARGETS=(
        __CPROVER_file_local_pipe_fs_i_h_pipe_buf_release
        pipe_buf_release
      )
      ;;
    lock_state)
      # mutex_unlock is an ordinary extern (not static inline), so
      # the external name suffices.
      CONTRACT_TARGETS=(
        mutex_unlock
      )
      ;;
    refcount_lifetime)
      # refcount_dec_and_test is static inline in modern kernels,
      # exposed under the mangled form __CPROVER_file_local_refcount
      # _h_refcount_dec_and_test.  Include both so whichever the
      # link resolves gets the contract.
      CONTRACT_TARGETS=(
        __CPROVER_file_local_refcount_h_refcount_dec_and_test
        refcount_dec_and_test
      )
      ;;
    aead)
      # aead_request_set_crypt is static inline in
      # <crypto/aead.h>.  The mangled form is the primary one
      # at kernel call sites; the external is for direct-call
      # harness links.
      CONTRACT_TARGETS=(
        __CPROVER_file_local_aead_h_aead_request_set_crypt
        aead_request_set_crypt
      )
      ;;
    *)
      echo "no default contract targets for '$MODULE'; pass explicitly" >&2
      exit 2
      ;;
  esac
fi

FULL_KERNEL_FILE=$(readlink -f -- "$LINUX_TREE/$KERNEL_FILE")
if [[ ! -f "$FULL_KERNEL_FILE" ]]; then
  echo "kernel file not found: $FULL_KERNEL_FILE" >&2
  exit 2
fi

stem=$(basename -- "$KERNEL_FILE" .c)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

KERNEL_GB="$tmp/${stem}.kernel.gb"
HARNESS_C="$tmp/${stem}_harness.c"
HARNESS_GB="$tmp/${stem}_harness.gb"
LINKED_GB="$tmp/${stem}.linked.gb"
INSTR_GB="$tmp/${stem}.instr.gb"

echo "[per-file] module=$MODULE file=$KERNEL_FILE function=$TARGET_FUNC"

echo "[1/7] compiling kernel TU..."
"$SCRIPT_DIR/compile_file.sh" "$LINUX_TREE" "$KERNEL_FILE" "$KERNEL_GB" \
  >"$tmp/compile.log" 2>&1 || {
    echo "  FAIL: compile_file.sh returned $? on $KERNEL_FILE" >&2
    tail -10 "$tmp/compile.log" >&2
    exit 3
  }

echo "[2/7] synthesising per-file harness..."
set +e
python3 "$SCRIPT_DIR/synthesise_harness.py" \
  "$MODULE" "$FULL_KERNEL_FILE" "$TARGET_FUNC" "$HARNESS_C" \
  2> "$tmp/synth.err"
synth_rc=$?
set -e
cat "$tmp/synth.err" >&2
if [[ $synth_rc -ne 0 ]]; then
  echo "  FAIL: synthesise_harness.py could not generate harness" >&2
  exit 3
fi
# Detect "empty ghost" condition (synthesise_harness emitted a
# warning to stderr).  We don't reclassify the verdict — some
# real bug-class patterns still get caught when the ghost is
# empty (e.g. back-to-back put_cred where neither parameter is
# a cred *).  But we do propagate the marker into the cbmc log
# so the per-file rollup can segment high- vs low-confidence
# candidates downstream if needed.
EMPTY_GHOST=0
if grep -q "harness ghost is empty" "$tmp/synth.err"; then
  EMPTY_GHOST=1
fi

echo "[3/7] compiling harness TU..."
# compile_file.sh expects a path relative to LINUX_TREE; drop the
# synthesised C file inside the tree temporarily so its kernel-
# header soup is on the -I path.
HARNESS_IN_TREE="$LINUX_TREE/_scan_per_file_harness_${stem}.c"
cp "$HARNESS_C" "$HARNESS_IN_TREE"
trap 'rm -rf "$tmp"; rm -f "$HARNESS_IN_TREE"' EXIT
"$SCRIPT_DIR/compile_file.sh" "$LINUX_TREE" \
  "_scan_per_file_harness_${stem}.c" "$HARNESS_GB" \
  >"$tmp/harness-compile.log" 2>&1 || {
    echo "  FAIL: compile_file.sh could not build the harness" >&2
    tail -10 "$tmp/harness-compile.log" >&2
    exit 3
  }

echo "[4/7] linking kernel TU + harness + adapter + property module..."
GOTOCC=${GOTOCC:-$REPO_ROOT/build/bin/goto-cc}
link_inputs=("$KERNEL_GB" "$HARNESS_GB" "$ADAPTER")
if [[ -n "$PROPERTY_SRC" ]]; then
  link_inputs+=("$PROPERTY_SRC")
fi
link_inputs+=("${EXTRA_LINK_SRCS[@]}")
"$GOTOCC" "${link_inputs[@]}" \
  -o "$LINKED_GB" >"$tmp/link.log" 2>&1 || {
    echo "  FAIL: goto-cc link returned $?" >&2
    tail -10 "$tmp/link.log" >&2
    exit 3
  }

echo "[5/7] applying contracts..."
GOTOINSTR=${GOTOINSTR:-$REPO_ROOT/build/bin/goto-instrument}
cur="$LINKED_GB"
for target in "${CONTRACT_TARGETS[@]}"; do
  next="$tmp/${stem}.trans.${target}.gb"
  if "$GOTOINSTR" --replace-call-with-contract "$target" \
       "$cur" "$next" >"$tmp/instr-${target}.log" 2>&1
  then
    cur="$next"
  else
    # Some targets may be absent from this link; ignore.
    echo "  (contract '$target' not applied — symbol absent or mismatch)"
  fi
done

echo "[6/7] dropping unreachable functions..."
# Drop function bodies that are trivially unreachable from main.
# Essential for large enclosing functions: e.g.
# fs/coredump.c:do_coredump pulls in hundreds of sibling
# kernel functions that the harness never calls.  Without
# this step, CBMC's symex has to process all of them before
# even starting on the path to the contract site, and the
# per-file budget is exhausted.
#
# We use --drop-unused-functions rather than the more
# aggressive --aggressive-slice because the latter can
# segfault inside goto-instrument on very large inputs (it
# does a full reachability + dead-store analysis); the
# lighter --drop-unused-functions is stable and delivers most
# of the value on per-file harnesses whose main calls the
# one synthesised entry point.
#
# Reachable-from-main preserves the property module's ghost
# helpers naturally because the main → harness → ghost-init
# call chain keeps them in the reachable set.
dropped="$tmp/${stem}.dropped.gb"
if "$GOTOINSTR" --drop-unused-functions "$cur" "$dropped" \
     >"$tmp/drop.log" 2>&1
then
  cur="$dropped"
else
  echo "  (drop-unused-functions failed or no-op; using full binary)"
fi
cp "$cur" "$INSTR_GB"

echo "[7/7] running cbmc on ${TARGET_FUNC}_per_file_harness..."
CBMC=${CBMC:-$REPO_ROOT/build/bin/cbmc}
entry="${TARGET_FUNC}_per_file_harness"
set +e
"$CBMC" \
  --function "$entry" \
  --unwind "${UNWIND:-3}" \
  --unwinding-assertions \
  --no-standard-checks \
  "$INSTR_GB" > "$tmp/cbmc.log" 2>&1
rc=$?
set -e

# Summarise: extract the contract-precondition assertions.
echo
echo "=== $KERNEL_FILE: $TARGET_FUNC ==="
grep -E "precondition\.[0-9]+\]" "$tmp/cbmc.log" | sed 's/^/  /' || true

# Refined classification: a per-file verdict only counts as a
# real candidate bug signal when CBMC reports a FAILURE on a
# contract clause (requires/ensures).  CBMC's built-in property
# checks (memcpy/memset bounds, no-body callees, unwinding
# assertions, etc.) fire routinely on partially-initialised
# harness state and would otherwise drown out real signal.
#
# Exit codes:
#   0  VERIFICATION SUCCESSFUL with at least one contract
#      clause checked and holding.
#   10 VERIFICATION FAILED with at least one contract clause
#      violated — REAL CANDIDATE.
#   11 VERIFICATION FAILED but no contract clause failed (only
#      CBMC built-ins fired) — infrastructure noise.
#   12 VERIFICATION SUCCESSFUL or FAILED but no contract clause
#      was even checked — vacuous.
#   2  usage error (set elsewhere in this script)
#   3  infrastructure error (compile/link, set elsewhere)
contract_pat='Check (requires|ensures) clause'
# Confidence suffix: distinguish FAILED-with-bootstrap (contract
# is firing on a synthesised ghost state — high-confidence
# candidate) from FAILED-empty-bootstrap (the harness's ghost
# was empty, so the contract fires by default whenever the
# ghost is consulted — lower-confidence: still useful when the
# function body has a real bug-class pattern, but more likely
# to be uniform noise at corpus scale).
if (( EMPTY_GHOST == 1 )); then
  conf=" [empty-ghost-confidence: low]"
else
  conf=""
fi
if grep -qE "$contract_pat.*FAILURE" "$tmp/cbmc.log"; then
  echo "  verdict: CONTRACT VIOLATION (real candidate)$conf"
  exit 10
elif grep -q "^VERIFICATION SUCCESSFUL\$" "$tmp/cbmc.log"; then
  if grep -qE "$contract_pat" "$tmp/cbmc.log"; then
    echo "  verdict: VERIFICATION SUCCESSFUL (contract holds)$conf"
    exit 0
  else
    echo "  verdict: VACUOUS (no contract clauses checked)$conf"
    exit 12
  fi
elif grep -q "^VERIFICATION FAILED\$" "$tmp/cbmc.log"; then
  if grep -qE "$contract_pat" "$tmp/cbmc.log"; then
    echo "  verdict: NOISE (built-in checks fired; no contract violation)$conf"
    exit 11
  else
    echo "  verdict: VACUOUS (no contract clauses checked; built-in failures only)$conf"
    exit 12
  fi
else
  echo "  verdict: (cbmc did not terminate with a verdict, rc=$rc)$conf"
  tail -5 "$tmp/cbmc.log" | sed 's/^/    /'
  exit $((rc == 0 ? 4 : rc))
fi
