#!/bin/sh
grep -q foo /etc/passwd
__CPROVER_assert "$?" -eq 0
d=$(date)
__CPROVER_assert "$d" != ""
echo hello | [ 1 -eq 1 ]
__CPROVER_assert "$?" -eq 0
