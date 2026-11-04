#!/bin/sh
# read assigns values that some input can give, and keeps the values of
# other variables.
k=kept
x=old
if read -r a b; then
  case "$a" in *" "* | *"	"*) __CPROVER_assert 0 = 1 ;; esac
  case "$a$b" in *"
"*) __CPROVER_assert 0 = 1 ;; esac
else
  __CPROVER_assert "$?" != 0
fi
__CPROVER_assert "$k" = kept
while IFS= read -r line < /dev/null; do
  case "$line" in *"
"*) __CPROVER_assert 0 = 1 ;; esac
  break
done
p=
IFS=: read -r p q < /dev/null
case "$p" in *:*) __CPROVER_assert 0 = 1 ;; esac
__CPROVER_assert "$IFS" = " 	
"
