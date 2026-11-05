#!/bin/bash
# [[ ... ]]: no field splitting, patterns on the right of ==, integer
# comparisons of arithmetic expressions, and short-circuit && and ||.
x="a b"
[[ $x == "a b" ]] || __CPROVER_assert 0 = 1
[[ $x == a* ]] || __CPROVER_assert 0 = 1
[[ $x == "a*" ]] && __CPROVER_assert 0 = 1
p="a?b"
[[ $x == $p ]] || __CPROVER_assert 0 = 1
[[ -n $x && ! -z $x ]] || __CPROVER_assert 0 = 1
n=3
[[ $n -eq 1+2 ]] || __CPROVER_assert 0 = 1
[[ $n -lt 2 || $n -ge 3 ]] || __CPROVER_assert 0 = 1
unset u
[[ $u ]] && __CPROVER_assert 0 = 1
[[ -v x ]] || __CPROVER_assert 0 = 1
unset y
[[ a == b && ${y=assigned} ]]
__CPROVER_assert "${y-unset}" = unset
[[ -f /etc/passwd ]] || :
