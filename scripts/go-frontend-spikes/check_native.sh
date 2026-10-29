#!/usr/bin/env bash
# Regression check for the native C++ Go parser (spike B), against the
# go/parser oracle. Needs a Go toolchain (go1.27.x) on PATH and a C++17
# compiler. Exits non-zero on any failure.
#
#   1. corpus/*.go and the whole GOROOT/src tree (excluding testdata) must
#      parse, with per-file node-kind counts identical to go/parser's.
#   2. Every file in corpus/invalid/ must be rejected (go/parser must
#      reject it too, so the expectation itself is checked).
#
# Usage: scripts/go-frontend-spikes/check_native.sh [--no-stdlib]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Bound memory and time of every heavy step (address space in KiB).
ulimit -v 8388608

echo "== building native parser"
timeout 300 "${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -o "$work/goparse" \
  "$here"/native/main.cpp "$here"/native/go_lexer.cpp \
  "$here"/native/go_parser.cpp
echo "== building go/parser oracle"
(cd "$here/oracle" && timeout 300 go build -o "$work/go_oracle" .)

ls "$here"/corpus/*.go "$here"/corpus/syntax/*.go > "$work/files.txt"
if [ "${1:-}" != "--no-stdlib" ]; then
  find "$(go env GOROOT)/src" -name '*.go' -not -path '*/testdata/*' \
    >> "$work/files.txt"
fi

echo "== parsing $(wc -l < "$work/files.txt") valid files"
timeout 600 "$work/goparse" --counts < "$work/files.txt" \
  > "$work/native.txt" || true # FAIL lines are reported by the comparison
timeout 600 "$work/go_oracle" < "$work/files.txt" > "$work/oracle.txt"
python3 "$here/oracle/compare_counts.py" "$work/native.txt" "$work/oracle.txt"

echo "== checking that invalid inputs are rejected"
status=0
for f in "$here"/corpus/invalid/*.go; do
  if gofmt -e -l "$f" > /dev/null 2>&1; then
    echo "  expectation wrong: go/parser accepts $f"
    status=1
  fi
  if echo "$f" | timeout 60 "$work/goparse" 2>/dev/null | grep -q '^OK'; then
    echo "  native parser wrongly accepts $f"
    status=1
  fi
done
[ "$status" -eq 0 ] && echo "  all $(ls "$here"/corpus/invalid/*.go | wc -l) rejected by both"
exit "$status"
