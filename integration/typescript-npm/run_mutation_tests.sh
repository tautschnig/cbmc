#!/usr/bin/env bash
#
# Mutation tests for npm integration harnesses.
#
# Rationale: harnesses can silently degrade into tautologies (e.g., if
# someone replaces a symbolic input with a concrete constant while
# "simplifying"). Mutation testing defends against this: we introduce
# known bugs into each harness and require that the bug is DETECTED
# (verification fails).
#
# If any mutation produces VERIFICATION SUCCESSFUL, the harness is not
# exercising the property it claims to — and this script exits non-zero.
#
# This script is invoked by CI alongside run_tests.sh.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CBMC="${CBMC:-$REPO_ROOT/build/bin/cbmc}"

if [ ! -x "$CBMC" ]; then
    echo "ERROR: CBMC binary not found at $CBMC"
    exit 1
fi

# Each mutation: harness_name:sed_script:description
# The sed script introduces a plausible bug. If the harness fails
# under this mutation, the harness effectively verifies the property.
# If it passes, the harness is tautological.
MUTATIONS=(
    'ms:s|if (ms >= D) return "d";|if (ms < D) return "d";|:wrong-comparison-in-day-boundary'
    'ms:s|if (ms >= H) return "h";|if (ms > H) return "h";|:off-by-one-at-hour-boundary'
    'classnames:s|if (a.length === 0) return b;|if (a.length === 1) return b;|:wrong-empty-check'
    'classnames:s|return a + " " + b;|return a + b;|:missing-separator'
    'uuid:s|this.version === other.version;|true;|:dropped-field-comparison'
    'uuid:s|this.high === other.high|true|:dropped-high-comparison'
    'left-pad:s|if (strLen >= len) return false; // no padding needed|if (strLen > len) return false;|:boundary-off-by-one'
    'left-pad:s|return len;|return strLen;|:wrong-return-value'
)

failures=0
total=${#MUTATIONS[@]}

echo "Running $total mutation tests (each must be DETECTED)..."
echo ""

for entry in "${MUTATIONS[@]}"; do
    # Parse entry: split by first ':', then next ':', rest is description
    harness="${entry%%:*}"
    rest="${entry#*:}"
    sed_script="${rest%:*}"
    description="${rest##*:}"

    harness_path="$SCRIPT_DIR/harness/${harness}.ts"
    if [ ! -f "$harness_path" ]; then
        echo "SKIP: ${harness}/${description} (no harness at $harness_path)"
        continue
    fi

    mutant="$(mktemp --suffix=.ts)"
    sed "$sed_script" "$harness_path" > "$mutant"

    # Verify the sed actually changed something (mutation must actually mutate).
    if diff -q "$harness_path" "$mutant" > /dev/null; then
        echo "ERROR: mutation for ${harness}/${description} didn't change anything"
        echo "  sed script: $sed_script"
        echo "  (the sed pattern may not match the current harness)"
        rm -f "$mutant"
        failures=$((failures + 1))
        continue
    fi

    # Run CBMC on the mutant under resource bounds.
    set +e
    ( ulimit -v 4000000 -t 120 && "$CBMC" "$mutant" > /tmp/mutant-out.$$ 2>&1 )
    cbmc_exit=$?
    set -e

    # Expect FAILURE (non-zero exit).
    if [ "$cbmc_exit" -eq 0 ]; then
        echo "TAUTOLOGY: ${harness}/${description}"
        echo "  The mutation was NOT caught. Harness is not exercising the property."
        echo "  sed script: $sed_script"
        failures=$((failures + 1))
    else
        echo "OK: ${harness}/${description} (mutation caught)"
    fi

    rm -f "$mutant" /tmp/mutant-out.$$
done

echo ""
echo "Results: $((total - failures))/$total mutations detected"

if [ $failures -gt 0 ]; then
    echo ""
    echo "ERROR: $failures harness(es) failed mutation testing."
    echo "This means one or more harnesses may have degenerated into tautologies."
    echo "Review the failing mutation(s) and strengthen the harness."
    exit 1
fi
