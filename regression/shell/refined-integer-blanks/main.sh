#!/bin/sh
[ " 12 " -eq 12 ]
__CPROVER_assert "$?" -eq 0
[ "	-3" -lt 0 ]
__CPROVER_assert "$?" -eq 0
[ "1 2" -eq 12 ] 2>/dev/null
__CPROVER_assert "$?" -eq 2
[ " " -eq 0 ] 2>/dev/null
__CPROVER_assert "$?" -eq 2
x=" 7 "
__CPROVER_assert $((x + 1)) -eq 8
y=" 0 "
__CPROVER_assert $((y)) -eq 0
