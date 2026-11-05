#!/bin/sh
# ${#*} and ${*<op>word}, where shells agree.
set -- a bc
__CPROVER_assert "${*:-none}" = "a bc"
__CPROVER_assert "${*+set}" = set
n=${#*}
__CPROVER_assert "$n" = 2 -o "$n" = 4
set --
__CPROVER_assert "${*:-none}" = none
__CPROVER_assert "${*:+some}" = ""
__CPROVER_assert "${#*}" = 0
__CPROVER_assert "${#@}" = 0
