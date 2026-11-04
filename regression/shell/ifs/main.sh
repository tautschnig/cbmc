#!/bin/sh
p="/usr/bin:/bin::/sbin"
OLDIFS="$IFS"
IFS=:
n=0
for d in $p; do n=$((n + 1)); done
__CPROVER_assert "$n" = 4
set -- $p
__CPROVER_assert "$3" = ""
__CPROVER_assert "$4" = /sbin
__CPROVER_assert "$*" = "/usr/bin:/bin::/sbin"
IFS="$OLDIFS"
__CPROVER_assert "$IFS" = " 	
"
q="a b"
set -- $q
__CPROVER_assert "$#" = 2
IFS=
set -- $q
__CPROVER_assert "$#" = 1
unset IFS
set -- $q
__CPROVER_assert "$#" = 2
__CPROVER_assert "${IFS-unset}" = unset
f() { set -- $1; __CPROVER_assert "$#" = 2; }
IFS=" "
unset IFS
f "x y"
