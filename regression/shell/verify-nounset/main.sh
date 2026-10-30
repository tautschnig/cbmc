#!/bin/sh
set -u
unset x
: "${x-}" "$#" "$@"
__CPROVER_assert reached = yes
: "$x"
__CPROVER_assert reached = no
