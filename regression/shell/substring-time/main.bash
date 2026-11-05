#!/bin/bash
# ${x:offset:length}, and the time keyword.
x=abcdef
__CPROVER_assert "${x:1:2}" = bc
__CPROVER_assert "${x:4}" = ef
__CPROVER_assert "${x:9}" = ""
__CPROVER_assert "${x:1:9}" = bcdef
i=1
__CPROVER_assert "${x:i+1:1}" = c
time false
__CPROVER_assert "$?" = 1
time -p true | cat
