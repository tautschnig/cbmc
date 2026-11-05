#!/bin/sh
# getopts with refined strings: an option with an argument.
set -- -b x y
getopts "ab:" opt
__CPROVER_assert "$opt" = b
__CPROVER_assert "$OPTARG" = x
shift $((OPTIND - 1))
__CPROVER_assert "$1" = y
