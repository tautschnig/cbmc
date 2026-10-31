#!/bin/sh
n=$1
# test accepts " 08" (blanks, leading zero), but $((...)) rejects it
# as an invalid octal constant: a real error that the verifier must find.
case $n in 0*) exit 0 ;; esac
if [ "$n" -ge 0 ] 2>/dev/null && [ "$n" -lt 1000 ]; then
  m=$((n + 1))
  __CPROVER_assert "$m" -gt "$n"
fi
