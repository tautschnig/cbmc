#!/bin/sh
# After the if, IFS is unset or ":": it is not known.
unset IFS
if [ "$1" = a ]; then IFS=:; fi
x=a:b
set -- $x
__CPROVER_assert "$#" = 1
