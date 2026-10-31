#!/bin/sh
i=0
while [ "$i" -lt 3 ]; do i=$((i + 1)); done
s="v$i"
__CPROVER_assert "$s" = v3
