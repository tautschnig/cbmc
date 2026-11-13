#!/bin/bash
# Decide whether a CBMC log actually supports a claim of proof.
#
# Prose rules did not stop three documents from carrying wrong claims, nor a
# harness from passing vacuously through four rounds.  This is the same five
# rules as an exit status.
#
#   check_hygiene.sh --proved  MAIN.log [--vacuity VAC.log] [--allow LABEL]... [--no-loops]
#   check_hygiene.sh --refuted MAIN.log --expect LABEL [--vacuity VAC.log]
#
# --proved   the run is claimed to prove its obligations
# --refuted  the run is claimed to reproduce a finding: the named label MUST
#            fail and everything else must still be clean
#
# Checks, in the order they most often catch something:
#   1. a verdict is present at all (not a timeout or a crash)
#   2. no UNKNOWN property
#   3. no reached bodyless callee, which cuts the path and makes later
#      assertions vacuous
#   4. unwinding assertions present and none failing
#   5. no unexpected failing property
#   6. if a vacuity log is given, it must contain witnesses and every witness
#      must FAIL
#
# Exit 0 if the claim stands, 1 if it does not, 2 on usage error.
set -u

mode=""; main=""; vac=""; expect=""; allow=(); noloops=0
while [ $# -gt 0 ]; do
  case "$1" in
    --proved)  mode=proved;  main=${2:?log}; shift 2;;
    --refuted) mode=refuted; main=${2:?log}; shift 2;;
    --vacuity) vac=${2:?log}; shift 2;;
    --expect)  expect=${2:?label}; shift 2;;
    --allow)   allow+=("${2:?label}"); shift 2;;
    --no-loops) noloops=1; shift;;
    *) echo "usage: $0 --proved|--refuted LOG [--vacuity LOG] [--expect L] [--allow L]..." >&2; exit 2;;
  esac
done
[ -n "$mode" ] && [ -n "$main" ] || { echo "$0: need --proved or --refuted with a log" >&2; exit 2; }
[ -f "$main" ] || { echo "FAIL: no such log: $main" >&2; exit 1; }
[ "$mode" = refuted ] && [ -z "$expect" ] && { echo "$0: --refuted needs --expect LABEL" >&2; exit 2; }

rc=0
say() { printf '  %-6s %s\n' "$1" "$2"; }
bad() { say FAIL "$1"; rc=1; }

# 1. a verdict at all
if grep -qE '^VERIFICATION (SUCCESSFUL|FAILED)$' "$main"; then
  verdict=$(grep -oE '^VERIFICATION (SUCCESSFUL|FAILED)$' "$main" | tail -1)
  say ok "verdict present: $verdict"
else
  bad "no verdict in $main (timeout, crash, or an unexecutable solver binary -- CBMC reports that last one as per-property ERROR with no diagnostic)"
  verdict=""
fi

# 2. UNKNOWN
n=$(grep -cE ': UNKNOWN$' "$main" || true)
[ "$n" -eq 0 ] && say ok "no UNKNOWN property" || bad "$n UNKNOWN property/properties: an undecided property is not a proven one"

# 3. bodyless callee actually reached
n=$(grep -cE 'no body for callee.*FAILURE' "$main" || true)
[ "$n" -eq 0 ] && say ok "no reached bodyless callee" || bad "$n reached bodyless callee(s): the path is cut, so later assertions may hold vacuously"

# 4. unwinding assertions
if grep -qE 'unwind(ing)?( assertion)?.*: (SUCCESS|FAILURE)' "$main"; then
  n=$(grep -cE 'unwind.*: FAILURE' "$main" || true)
  [ "$n" -eq 0 ] && say ok "unwinding assertions present and passing" \
                 || bad "$n failing unwinding assertion(s): a loop was cut, so the result covers only the unwound prefix"
elif [ "$noloops" = 1 ]; then
  say ok "no unwinding assertion, and --no-loops asserts there are no loops"
else
  bad "no unwinding assertion in the log: pass --unwinding-assertions to CBMC, or --no-loops here to state that this harness has none"
fi

# 5. failing properties
pat='vacuity witness'
for a in ${allow+"${allow[@]}"}; do pat="$pat|$a"; done
[ "$mode" = refuted ] && pat="$pat|$expect"
n=$(grep -E ': FAILURE$' "$main" | grep -vcE "$pat" || true)
[ "$n" -eq 0 ] && say ok "no unexpected failing property" || {
  bad "$n unexpected failing property/properties:"
  grep -E ': FAILURE$' "$main" | grep -vE "$pat" | sed 's/^/           /' | head -5
}

if [ "$mode" = refuted ]; then
  if grep -qE "$expect.*: FAILURE$" "$main"; then
    say ok "the expected finding '$expect' does fail"
  else
    bad "'$expect' does not fail: the run no longer reproduces the finding it claims to"
  fi
elif [ -n "$verdict" ] && [ "$verdict" != "VERIFICATION SUCCESSFUL" ]; then
  bad "claimed proved, but the verdict is $verdict"
fi

# 6. vacuity witnesses
if [ -n "$vac" ]; then
  if [ ! -f "$vac" ]; then
    bad "no such vacuity log: $vac"
  else
    w=$(grep -cE 'vacuity witness' "$vac" || true)
    if [ "$w" -eq 0 ]; then
      bad "the vacuity log contains no witness: nothing establishes that the state the property describes can arise"
    else
      p=$(grep -E 'vacuity witness.*: SUCCESS$' "$vac" | wc -l)
      [ "$p" -eq 0 ] && say ok "$w vacuity witness(es), all FAILING as they must" || {
        bad "$p witness(es) did NOT fail -- the state they describe may be unreachable, so the proof can be empty:"
        grep -E 'vacuity witness.*: SUCCESS$' "$vac" | sed 's/^/           /' | head -5
      }
    fi
  fi
else
  say note "no vacuity log given: non-vacuity is unchecked"
fi

[ $rc -eq 0 ] && echo "  CLAIM STANDS" || echo "  CLAIM NOT SUPPORTED"
exit $rc
