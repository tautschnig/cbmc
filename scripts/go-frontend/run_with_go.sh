#!/bin/bash
# Run regression/go tests with the Go toolchain as an oracle.
#
# Each test's main.go is built and run (for GOARCH=386 if the test uses
# --32) with `go run` (`tinygo run` if it uses --go-compiler tinygo),
# using the real
# package cprover (src/go/helper/cprover): Assert panics if its condition
# is false and Nondet returns the zero value. So every expected
# VERIFICATION SUCCESSFUL test must run to completion; for tests expecting
# VERIFICATION FAILED, the result is only reported (the failing execution
# may need nondeterministic values that Nondet does not produce).
#
# This checks the tests' expectations against the reference implementation
# of the Go specification, independently of CBMC.
#
# Usage: scripts/go-frontend/run_with_go.sh [TEST-DIR...]
# (default: all of regression/go). Needs `go` (1.27) on PATH.
# Exits 1 if a test expected to succeed does not.

set -u

root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# A module that resolves the import path "cprover" to the real package.
mkdir -p "$work/cprover"
cp "$root/src/go/helper/cprover/cprover.go" "$work/cprover/"
printf 'module cprover\n\ngo 1.27\n' > "$work/cprover/go.mod"

if [ $# -eq 0 ]; then
  set -- "$root"/regression/go/*/
fi

status=0
for dir in "$@"; do
  dir="${dir%/}"
  name="$(basename "$dir")"
  desc="$dir/test.desc"
  [ -f "$dir/main.go" ] && [ -f "$desc" ] || continue
  # test.desc: tag, source, options, then the expected patterns up to the
  # first "--" line (test.pl's format).
  options="$(sed -n 3p "$desc")"
  expected="$(sed -n '4,${/^--$/q;p}' "$desc")"
  goarch=""
  case " $options " in
    *" --32 "*) goarch=386 ;;
  esac
  # Tests of the front-end's runtime package (cprover/rt, whose intrinsics
  # have no Go implementation) cannot run with Go.
  if grep -q '"cprover/rt"' "$dir"/*.go; then
    echo "skip    $name: uses cprover/rt"
    continue
  fi
  # Tests of TinyGo's choices (--go-compiler tinygo) run with TinyGo, and
  # are skipped without it.
  runner="go run"
  case " $options " in
    *" --go-compiler tinygo "*)
      if ! command -v tinygo > /dev/null; then
        echo "skip    $name: needs tinygo"
        continue
      fi
      runner="tinygo run" ;;
  esac
  # The test's directory is its package (and, with a go.mod, its module,
  # which may have further packages); it gets the real package cprover.
  rm -rf "$work/main"
  cp -r "$dir" "$work/main"
  [ -f "$work/main/go.mod" ] ||
    printf 'module oracle\n\ngo 1.27\n' > "$work/main/go.mod"
  (cd "$work/main" && GOTOOLCHAIN=local go mod edit \
    -require=cprover@v0.0.0 -replace=cprover="$work/cprover")
  output="$(cd "$work/main" && env GOTOOLCHAIN=local GOFLAGS=-mod=mod \
    ${goarch:+GOARCH=$goarch} timeout 300 $runner . 2>&1)"
  result=$?
  if echo "$expected" | grep -q '^\^VERIFICATION SUCCESSFUL\$$'; then
    if [ $result -eq 0 ]; then
      echo "ok      $name"
    else
      echo "MISMATCH $name: expected to succeed, $runner exited $result"
      echo "$output" | sed 's/^/    /' | head -5
      status=1
    fi
  else
    # Not a success test: report what Go does (e.g., the panic message).
    echo "info    $name: $runner exited $result: $(echo "$output" | head -1)"
  fi
done
exit $status
