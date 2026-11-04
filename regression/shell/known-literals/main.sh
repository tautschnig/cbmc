#!/bin/bash
# The literal values of variables, used during conversion, must hold on
# every path. Each check below is false if a value from another path
# leaks in.
x=one
if [ "$1" = a ]; then x=two; else __CPROVER_assert "$x" = one; fi
case "$1" in a) __CPROVER_assert "$x" = two ;; *) __CPROVER_assert "$x" = one ;; esac
y=start
for i in 1 2; do
  __CPROVER_assert "$y" = start
  [ "$1" = c ] && continue
  y=start
done
z=a
while [ "$z" = a ]; do
  [ "$2" = q ] && break
  z=b
done
[ "$2" = q ] && __CPROVER_assert "$z" = a
[ "$2" = q ] || __CPROVER_assert "$z" = b
f() { w=changed; }
w=orig
f
__CPROVER_assert "$w" = changed
v=outer
(v=inner)
__CPROVER_assert "$v" = outer
u=1
: $((u = 5))
__CPROVER_assert "$u" = 5
t=set
unset t
__CPROVER_assert "${t-unset}" = unset
s=
: "${s:=filled}"
__CPROVER_assert "$s" = filled
cmd=f
w=orig
$cmd
__CPROVER_assert "$w" = changed
case "$3" in
  a) r=from-a ;&
  b) if [ "$3" = a ]; then __CPROVER_assert "$r" = from-a; fi ;;
esac
list="p q r"
n=0
for e in $list; do n=$((n + 1)); done
__CPROVER_assert "$n" = 3
