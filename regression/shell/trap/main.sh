#!/bin/sh
# The EXIT trap runs when the shell exits, with $? the exit status, which
# it keeps unless it exits; subshells reset traps, and run their own.
cleanup() {
  __CPROVER_assert "$?" = 3
  ran=yes
  false
}
trap cleanup EXIT HUP INT QUIT TERM
trap '' PIPE
x=$(trap 'exit 5' EXIT; exit 1)
__CPROVER_assert "$?" = 5
(trap - EXIT; exit 2)
__CPROVER_assert "$?" = 2
trap "__CPROVER_assert \$? = 3" 0
trap cleanup EXIT
exit 3
