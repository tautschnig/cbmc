#!/bin/sh
set -- q 5
[ 1 = 2 -a x -eq 1 ] 2>/dev/null; __CPROVER_assert "$?" = 2
[ 1 = 1 -o x -eq 1 ] 2>/dev/null; __CPROVER_assert "$?" = 2
[ 1 = 1 -a x -eq 1 ] 2>/dev/null; __CPROVER_assert "$?" = 2
[ 1 = 2 -o 1 = 1 -a 1 = 2 ] 2>/dev/null; __CPROVER_assert "$?" = 1
[ ! 1 = 1 -o 1 = 1 ] 2>/dev/null; __CPROVER_assert "$?" = 0
[ \( 1 = 2 -o 1 = 1 \) -a 1 = 1 ] 2>/dev/null; __CPROVER_assert "$?" = 0
[ -n a -a -z "" ] 2>/dev/null; __CPROVER_assert "$?" = 0
[ a -a b -a "" ] 2>/dev/null; __CPROVER_assert "$?" = 1
[ -z "$1" -o -n "$1" ] 2>/dev/null; __CPROVER_assert "$?" = 0
[ "$1" = x -a "$1" != x ] 2>/dev/null; __CPROVER_assert "$?" = 1
[ ! -z a -a ! -n "" ] 2>/dev/null; __CPROVER_assert "$?" = 0
[ 1 -lt 2 -a 2 -lt 3 -a 3 -lt 4 ] 2>/dev/null; __CPROVER_assert "$?" = 0
[ \( a = b \) -o \( c = c \) ] 2>/dev/null; __CPROVER_assert "$?" = 0
[ "$2" -eq 1 -o 1 -eq 1 ] 2>/dev/null; __CPROVER_assert "$?" = 0
