#!/bin/sh
# The status after the trap is the exit status, unless the action exits;
# exit without an operand in the action exits with the exit status.
s=$(trap 'false' EXIT; exit 3; :) ; __CPROVER_assert "$?" = 3
s=$(trap 'false; exit' EXIT; exit 4) ; __CPROVER_assert "$?" = 4
s=$(trap 'exit 7' EXIT; exit 4) ; __CPROVER_assert "$?" = 7
s=$(trap 'exit 7' EXIT; trap 0; exit 4) ; __CPROVER_assert "$?" = 4
s=$(trap '__CPROVER_assert 0 = 1' EXIT; exec true)
s=$(set -e; trap 'exit 9' EXIT; false; exit 1) ; __CPROVER_assert "$?" = 9
