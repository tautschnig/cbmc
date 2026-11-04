#!/bin/sh
# Unquoted $@: each positional parameter is split, and empty ones vanish.
count() { __CPROVER_assert "$#" = "$n"; }
set --
n=0
count $@
set -- " a b " ""
n=2
count $@
f() { __CPROVER_assert "$1$2" = ab; }
f $@
