#!/bin/sh
# A redirection of a compound command can fail, which skips the command
# with a non-zero status; redirections that cannot fail change nothing.
x=0
{ x=1; } > /dev/null 2>&1
__CPROVER_assert "$x" = 1
if { x=2; } > "$1"; then
  __CPROVER_assert "$x" = 2
else
  __CPROVER_assert "$?" != 0
  __CPROVER_assert "$x" = 1
fi
while read -r line; do
  n=$line
  break
done < "$1"
unset y
for i in a; do y=$i; done 2> "$1" || __CPROVER_assert "${y-unset}" = unset
