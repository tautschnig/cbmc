#!/bin/bash
# bash sets UID, EUID and PPID to decimal numbers, and makes them read-only.
if [ "$EUID" -ne 0 ]; then
  u=$EUID
fi
__CPROVER_assert "$EUID" -ge 0
__CPROVER_assert "${#UID}" -ge 1
case "$PPID" in "" | *[!0-9]*) __CPROVER_assert 0 = 1 ;; esac
