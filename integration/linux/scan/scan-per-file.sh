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
#   6. Run cbmc on the harness entry function.
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
# Most modules use their own name; cred_lifetime's adapter is named
# 'cred_kernel_adapter.c' by history.  Resolve accordingly.
case "$MODULE" in
  cred_lifetime) ADAPTER_STEM=cred ;;
  *)             ADAPTER_STEM=$MODULE ;;
esac
ADAPTER="$SCRIPT_DIR/adapters/${ADAPTER_STEM}_kernel_adapter.c"
PROPERTY_SRC="$SCRIPT_DIR/../properties/${MODULE}/${MODULE}.c"

if [[ ! -f "$ADAPTER" ]]; then
  echo "no adapter for module '$MODULE' at $ADAPTER" >&2
  exit 2
fi
if [[ ! -f "$PROPERTY_SRC" ]]; then
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

echo "[1/6] compiling kernel TU..."
"$SCRIPT_DIR/compile_file.sh" "$LINUX_TREE" "$KERNEL_FILE" "$KERNEL_GB" \
  >"$tmp/compile.log" 2>&1 || {
    echo "  FAIL: compile_file.sh returned $? on $KERNEL_FILE" >&2
    tail -10 "$tmp/compile.log" >&2
    exit 3
  }

echo "[2/6] synthesising per-file harness..."
python3 "$SCRIPT_DIR/synthesise_harness.py" \
  "$MODULE" "$FULL_KERNEL_FILE" "$TARGET_FUNC" "$HARNESS_C" || {
    echo "  FAIL: synthesise_harness.py could not generate harness" >&2
    exit 3
  }

echo "[3/6] compiling harness TU..."
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

echo "[4/6] linking kernel TU + harness + adapter + property module..."
GOTOCC=${GOTOCC:-$REPO_ROOT/build/bin/goto-cc}
"$GOTOCC" "$KERNEL_GB" "$HARNESS_GB" "$ADAPTER" "$PROPERTY_SRC" \
  -o "$LINKED_GB" >"$tmp/link.log" 2>&1 || {
    echo "  FAIL: goto-cc link returned $?" >&2
    tail -10 "$tmp/link.log" >&2
    exit 3
  }

echo "[5/6] applying contracts..."
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
cp "$cur" "$INSTR_GB"

echo "[6/6] running cbmc on ${TARGET_FUNC}_per_file_harness..."
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

if grep -q "^VERIFICATION SUCCESSFUL\$" "$tmp/cbmc.log"; then
  echo "  verdict: VERIFICATION SUCCESSFUL"
  exit 0
elif grep -q "^VERIFICATION FAILED\$" "$tmp/cbmc.log"; then
  echo "  verdict: VERIFICATION FAILED"
  exit 10
else
  echo "  verdict: (cbmc did not terminate with a verdict, rc=$rc)"
  tail -5 "$tmp/cbmc.log" | sed 's/^/    /'
  exit $((rc == 0 ? 4 : rc))
fi
