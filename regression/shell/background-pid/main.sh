#!/bin/sh
# $! is unset before any asynchronous list, and a process ID after one.
__CPROVER_assert "$!" = ""
sleep 1 &
pid=$!
case "$pid" in "" | *[!0-9]*) __CPROVER_assert 0 = 1 ;; esac
__CPROVER_assert "$pid" != 0
