#!/bin/sh
# Command substitutions in arithmetic run first; their output must be an
# integer constant.
x=$(( $(echo 6) / 2 ))
case "$x" in "" | *[!0-9-]*) __CPROVER_assert 0 = 1 ;; esac
n=3
y=$(($(printf '%s' "$n") + n * 2))
