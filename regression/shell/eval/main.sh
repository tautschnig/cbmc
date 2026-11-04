#!/bin/sh
eval x=1
__CPROVER_assert "$x" = 1
assignment="y=two"
eval "$assignment"
__CPROVER_assert "$y" = two
z=orig
words="z=a b"
eval $words 2>/dev/null
__CPROVER_assert "$z" = orig
eval 'if [ "$x" = 1 ]; then w=yes; else w=no; fi'
__CPROVER_assert "$w" = yes
eval
__CPROVER_assert "$?" = 0
eval false
__CPROVER_assert "$?" = 1
