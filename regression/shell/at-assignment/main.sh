#!/bin/sh
# "$@" in an assignment joins the fields with a space where IFS starts
# with one; assignments to _ are allowed.
set -- a "b c" d
x="$@"
__CPROVER_assert "$x" = "a b c d"
y=$@
__CPROVER_assert "$y" = "a b c d"
set --
e="$@"
__CPROVER_assert "${e-unset}" = ""
for _ in 1 2; do :; done
read -r _ < /dev/null
