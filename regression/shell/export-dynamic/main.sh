#!/bin/sh
# export of names that are expansions, and export -p.
A=1
names="A B"
export $names
export "$names" 2>/dev/null || :
__CPROVER_assert "$A" = 1
export -p > /dev/null
