#!/bin/bash
# Check that regression/go success tests are not vacuous.
#
# For every test expecting VERIFICATION SUCCESSFUL, append
#   cprover.Assert(false, "non-vacuity")
# at the end of func main and run cbmc with the test's options: the new
# assertion must be reported as FAILURE. If it is not, the end of main is
# unreachable (e.g., because of an assumption that is always false), and the
# test's assertions hold vacuously.
#
# Usage: scripts/go-frontend/check_non_vacuous.sh CBMC [TEST-DIR...]
# (default: all of regression/go). Exits 1 if a test is vacuous.

set -u

if [ $# -lt 1 ]; then
  echo "usage: $0 CBMC [TEST-DIR...]" >&2
  exit 2
fi
cbmc="$(realpath "$1")"
shift
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [ $# -eq 0 ]; then
  set -- "$root"/regression/go/*/
fi

status=0
for dir in "$@"; do
  dir="${dir%/}"
  name="$(basename "$dir")"
  desc="$dir/test.desc"
  [ -f "$dir/main.go" ] && [ -f "$desc" ] || continue
  expected="$(sed -n '4,${/^--$/q;p}' "$desc")"
  echo "$expected" | grep -q '^\^VERIFICATION SUCCESSFUL\$$' || continue
  options="$(sed -n 3p "$desc")"

  # A copy of the test's directory (it may have further files and
  # packages), in which the closing brace of main, the first line "}" after
  # "func main() {", is preceded by the assertion.
  rm -rf "$work/test"
  cp -r "$dir" "$work/test"
  awk '
    /^func main\(\) \{$/ { in_main = 1 }
    in_main && /^\}$/ {
      print "\tcprover.Assert(false, \"non-vacuity\")"
      in_main = 0
    }
    { print }
  ' "$dir/main.go" > "$work/test/main.go"
  if ! grep -q '"non-vacuity"' "$work/test/main.go"; then
    echo "SKIPPED $name: no func main() found"
    continue
  fi

  # shellcheck disable=SC2086 # options are a word list
  output="$(cd "$work/test" && timeout 300 "$cbmc" main.go $options 2>&1)"
  if echo "$output" | grep -q 'non-vacuity: FAILURE$'; then
    echo "ok      $name"
  else
    echo "VACUOUS $name"
    echo "$output" | grep -E 'non-vacuity|ERROR|VERIFICATION' | sed 's/^/    /'
    status=1
  fi
done
exit $status
