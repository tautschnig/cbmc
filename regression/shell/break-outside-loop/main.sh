#!/bin/sh
# break and continue outside a loop do nothing, with status 0.
false
break
__CPROVER_assert "$?" = 0
continue 2
reached=yes
__CPROVER_assert "$reached" = yes
