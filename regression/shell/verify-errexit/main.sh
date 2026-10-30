#!/bin/sh
set -e
false || true
false && true
if false; then :; fi
while false; do :; done
! true
{ false && true; }
false | true
f() { false; r=after-false; }
if f; then __CPROVER_assert "$r" = after-false; fi
f || :
__CPROVER_assert reached = yes
f
__CPROVER_assert reached = no
