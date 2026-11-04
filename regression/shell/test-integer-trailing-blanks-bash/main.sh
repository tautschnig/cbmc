#!/bin/bash
# bash accepts isspace() bytes before an integer operand of test, but only
# space and tab after it.
x="	5 	"
[ "$x" -eq 5 ]
__CPROVER_assert "$?" = 0
x="5
"
[ "$x" -eq 5 ] 2>/dev/null
__CPROVER_assert "$?" = 2
x="5"
[ "$x" -eq 5 ] 2>/dev/null
__CPROVER_assert "$?" = 2
