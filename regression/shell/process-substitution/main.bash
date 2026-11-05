#!/bin/bash
# <(list) and >(list) run asynchronously and expand to a file name; $?
# is that of the assignment.
false
f=<(true)
__CPROVER_assert "$?" = 0
case "$f" in /dev/fd/*) ;; *) __CPROVER_assert 0 = 1 ;; esac
while read -r line; do :; break; done < <(printf 'a\n')
x=0
: <(x=1)
__CPROVER_assert "$x" = 0
__CPROVER_assert $[1 + 2] = 3
