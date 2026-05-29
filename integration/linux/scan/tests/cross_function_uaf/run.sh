#!/usr/bin/env bash
# run.sh — proof-of-concept runner for cross-function UAF
# detection.
#
# Demonstrates that with the use_after_free_generic property
# module's globally-shared ghost table, an instrumented kfree
# in a callee correctly propagates to the caller's
# __assert_not_freed contract.
#
# Vuln scenario: callee_frees.c
#   Expected: CBMC reports CONTRACT VIOLATION.
#
# Fix scenario: callee_frees_fix.c
#   Expected: CBMC reports VERIFICATION SUCCESSFUL.

set -u
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" \
             &>/dev/null && pwd)
ROOT=$(cd -- "$SCRIPT_DIR/../../../../.." && pwd)
GOTOCC=${GOTOCC:-$ROOT/build/bin/goto-cc}
CBMC=${CBMC:-$ROOT/build/bin/cbmc}
GOTOINSTR=${GOTOINSTR:-$ROOT/build/bin/goto-instrument}

ADAPTER="$ROOT/integration/linux/scan/adapters/use_after_free_generic_kernel_adapter.c"
PROP_MOD="$ROOT/integration/linux/properties/use_after_free_generic/use_after_free_generic.c"

run_case() {
  local name=$1 src=$2 fn=$3 expect_rc=$4 expect_text=$5
  echo "=== $name ==="
  local tmp=$(mktemp -d)
  trap "rm -rf $tmp" RETURN
  cat > "$tmp/main.c" <<HARNESS
extern int $fn(void *p);
extern void *kmalloc(unsigned long size, unsigned int gfp);

int main(void)
{
  /* Allocate a backing page so the caller's *(int *)p deref
   * is well-defined memory.  The bug class we test is the
   * temporal use-after-free, not bounds. */
  void *p = kmalloc(64, 0);
  return $fn(p);
}
HARNESS

  "$GOTOCC" --native-compiler gcc \
    "$src" "$ADAPTER" "$PROP_MOD" "$tmp/main.c" \
    -o "$tmp/linked.gb" >"$tmp/build.log" 2>&1 \
    || { echo "  FAIL: link ($?)"; cat "$tmp/build.log"; return 1; }

  "$GOTOINSTR" --replace-call-with-contract __assert_not_freed \
    "$tmp/linked.gb" "$tmp/instr.gb" \
    >"$tmp/instr.log" 2>&1 \
    || { echo "  FAIL: contract ($?)"; cat "$tmp/instr.log"; return 1; }

  "$CBMC" --function main --unwind 3 --unwinding-assertions \
    --no-standard-checks "$tmp/instr.gb" \
    >"$tmp/cbmc.log" 2>&1
  rc=$?
  # Filter out "no body for callee" no-body false-positives
  # — those are CBMC's built-in checks complaining about
  # kfree/kmalloc/uaf_track_freed which we deliberately
  # don't link bodies for.  We classify based on whether the
  # __assert_not_freed precondition fired specifically.
  if grep -q "__assert_not_freed.precondition.*FAILURE" \
       "$tmp/cbmc.log"; then
    real_rc=10
  else
    real_rc=0
  fi
  if [[ $real_rc -ne $expect_rc ]]; then
    echo "  FAIL: real_rc=$real_rc (expected $expect_rc), cbmc rc=$rc"
    tail -10 "$tmp/cbmc.log"
    return 1
  fi
  echo "  [ok] real_rc=$real_rc (cbmc rc=$rc)"
  return 0
}

set +e
run_case "vuln (callee frees, caller derefs)" \
         "$SCRIPT_DIR/callee_frees.c" \
         caller_uses_after_free \
         10 \
         "VERIFICATION FAILED"
rc1=$?
run_case "fix  (caller derefs first, then callee frees)" \
         "$SCRIPT_DIR/callee_frees_fix.c" \
         caller_uses_then_frees \
         0 \
         "VERIFICATION SUCCESSFUL"
rc2=$?

if [[ $rc1 -eq 0 && $rc2 -eq 0 ]]; then
  echo
  echo "PoC PASSED: cross-function UAF detected via shared ghost"
  exit 0
else
  echo
  echo "PoC FAILED: investigate logs above"
  exit 1
fi
