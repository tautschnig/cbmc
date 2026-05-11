#!/usr/bin/env bash
#
# Coverage report for the TypeScript frontend (src/typescript/).
# Builds CBMC with gcov instrumentation, runs the TypeScript
# regression suite, and generates an HTML report.
#
# Usage:
#   scripts/ts_coverage.sh                # full: build + tests + report
#   scripts/ts_coverage.sh --report-only  # skip build and tests, just regenerate report
#
# Output:
#   build-cov/coverage-ts/html/index.html   # browseable HTML report
#   build-cov/coverage-ts/ts.info           # lcov tracefile
#
# Requires: lcov (apt-get install lcov), g++, cmake.

set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR="$REPO_ROOT/build-cov"
OUT_DIR="$BUILD_DIR/coverage-ts"

REPORT_ONLY=0
if [ "${1:-}" = "--report-only" ]; then
  REPORT_ONLY=1
fi

if [ "$REPORT_ONLY" -eq 0 ]; then
  echo "==> Configuring coverage build at $BUILD_DIR"
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -Denable_coverage=1 -DCMAKE_CXX_COMPILER=g++ >/dev/null

  echo "==> Building cbmc"
  cmake --build "$BUILD_DIR" --target cbmc -j"$(nproc)"

  echo "==> Clearing previous gcda counters"
  find "$BUILD_DIR" -name "*.gcda" -delete 2>/dev/null || true

  # Stop any leftover daemon from a previous run
  unset CBMC_TS_SERVER_SOCKET
  "$SCRIPT_DIR/cbmc_ts_server" stop >/dev/null 2>&1 || true

  echo "==> Running TypeScript regression (no daemon, to exercise one-shot node path)"
  (cd "$REPO_ROOT/regression/typescript" && \
     timeout 1800 perl ../test.pl -e -p -c "$BUILD_DIR/bin/cbmc" -C 2>&1 | \
     tail -3)

  echo "==> Exercising --trace path (for expr2typescript coverage)"
  # Small failing program covers basic number/bool/string in the trace formatter.
  tmpd=$(mktemp -d)
  cat >"$tmpd/trace.ts" <<'EOF'
const n: number = nondet_number();
const b: boolean = nondet_boolean();
const s: string = "hello";
const arr: number[] = [1, 2, 3];
class C { x: number = 0; }
const c: C = new C();
__CPROVER_assume(n > 0);
console.assert(n > 1000000);
EOF
  timeout 30 "$BUILD_DIR/bin/cbmc" --trace "$tmpd/trace.ts" >/dev/null 2>&1 || true
  rm -rf "$tmpd"
fi

echo "==> Capturing lcov tracefile"
mkdir -p "$OUT_DIR"
lcov --ignore-errors mismatch,empty --capture --directory "$BUILD_DIR" \
  --output-file "$OUT_DIR/all.info" 2>&1 | tail -1

echo "==> Extracting src/typescript/ subset"
lcov --extract "$OUT_DIR/all.info" '*/src/typescript/*' \
  --output-file "$OUT_DIR/ts.info" 2>&1 | tail -1

echo "==> Rendering HTML report"
genhtml --ignore-errors unmapped,inconsistent,source \
  "$OUT_DIR/ts.info" --output-directory "$OUT_DIR/html" 2>&1 | tail -5

echo
echo "==> Summary"
lcov --summary "$OUT_DIR/ts.info" 2>&1 | tail -5
echo
echo "Browse: file://$OUT_DIR/html/index.html"
