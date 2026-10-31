#!/bin/sh
n=$1
# A leading 0 is octal in $((...)) but decimal in test.
case $n in 0*) exit 0 ;; esac
if [ "$n" -ge 0 ] 2>/dev/null && [ "$n" -lt 1000 ]; then
  m=$((n * 2))
  __CPROVER_assert "$m" -ne 10
fi
