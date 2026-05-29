#!/usr/bin/env bash
# run-cocci.sh — Phase 2 PoC runner: validates that the
# extended use_after_free_generic.cocci automatically
# inserts mark_freed() after each kfree-family call AND
# __assert_not_freed() before each x->fld access.  When
# the callee's kfree is in one TU and the caller's deref
# is in another, the shared static ghost table in the
# property module propagates the freed flag across function
# boundaries — without manual instrumentation.

set -u
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" \
             &>/dev/null && pwd)
ROOT=$(cd -- "$SCRIPT_DIR/../../../../.." && pwd)
GOTOCC=${GOTOCC:-$ROOT/build/bin/goto-cc}
CBMC=${CBMC:-$ROOT/build/bin/cbmc}
GOTOINSTR=${GOTOINSTR:-$ROOT/build/bin/goto-instrument}

ADAPTER="$ROOT/integration/linux/scan/adapters/use_after_free_generic_kernel_adapter.c"
PROP_MOD="$ROOT/integration/linux/properties/use_after_free_generic/use_after_free_generic.c"
COCCI="$ROOT/integration/linux/scan/tools/instrument-cocci/use_after_free_generic.cocci"

run_case() {
  local name=$1 src=$2 fn=$3 expect_rc=$4
  echo "=== $name ==="
  local tmp=$(mktemp -d)
  trap "rm -rf $tmp" RETURN

  # Apply cocci to the kernel TU — this is what scan-per-file
  # does in production.  Phase 2 inserts mark_freed() after
  # each kfree call automatically.
  cp "$src" "$tmp/kernel.c"
  cat > "$tmp/kernel-decls.h" <<HDR
extern void mark_freed(const void *p);
extern void __assert_not_freed(const void *p);
HDR
  # Prepend declarations (cocci doesn't add them).
  cat "$tmp/kernel-decls.h" "$tmp/kernel.c" > "$tmp/kernel-instrumented.c"
  spatch --sp-file "$COCCI" \
    --in-place --very-quiet "$tmp/kernel-instrumented.c" 2>/dev/null

  cat > "$tmp/main.c" <<HARNESS
struct foo { int v; };
extern int $fn(struct foo *p);
extern void *kmalloc(unsigned long size, unsigned int gfp);

int main(void)
{
  struct foo *p = kmalloc(sizeof(*p), 0);
  return $fn(p);
}
HARNESS

  "$GOTOCC" --native-compiler gcc \
    "$tmp/kernel-instrumented.c" "$ADAPTER" "$PROP_MOD" "$tmp/main.c" \
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
  if grep -q "__assert_not_freed.precondition.*FAILURE" "$tmp/cbmc.log"; then
    real_rc=10
  else
    real_rc=0
  fi
  if [[ $real_rc -ne $expect_rc ]]; then
    echo "  FAIL: real_rc=$real_rc (expected $expect_rc), cbmc rc=$rc"
    grep -E "precondition|FAILURE|VERIFICATION" "$tmp/cbmc.log" | head -5
    return 1
  fi
  echo "  [ok] real_rc=$real_rc (cbmc rc=$rc)"
  return 0
}

set +e
run_case "vuln (caller→callee, deref-after-free via x->fld)" \
         "$SCRIPT_DIR/cross_uaf_struct.c" \
         caller_uses_after_free_struct \
         10
rc1=$?
run_case "fix  (deref before free)" \
         "$SCRIPT_DIR/cross_uaf_struct_fix.c" \
         caller_uses_then_frees_struct \
         0
rc2=$?

if [[ $rc1 -eq 0 && $rc2 -eq 0 ]]; then
  echo
  echo "Phase 2 PoC PASSED: cocci auto-inserted mark_freed/" \
       "__assert_not_freed; cross-TU ghost propagation works"
  exit 0
else
  echo
  echo "Phase 2 PoC FAILED"
  exit 1
fi
