#!/bin/sh
i=0
for a in "$@"; do i=$((i + 1)); done
__CPROVER_assert "$i" -lt 8
