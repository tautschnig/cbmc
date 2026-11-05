#!/bin/sh
# getopts: grouped options, arguments in the same or the next word, the
# end of the options, and OPTIND reset by the script.
set -- -ac -b x -bz -- rest
seen=
while getopts "ab:c" opt; do
  case "$opt" in
    a | c) seen="$seen$opt" ;;
    b) seen="$seen b=$OPTARG" ;;
    *) __CPROVER_assert 0 = 1 ;;
  esac
done
__CPROVER_assert "$seen" = "ac b=x b=z"
__CPROVER_assert "$OPTIND" = 6
__CPROVER_assert "$opt" = "?"
shift $((OPTIND - 1))
__CPROVER_assert "$1" = rest
set -- -x -b
OPTIND=1
getopts ":b:" opt
__CPROVER_assert "$opt" = "?"
__CPROVER_assert "$OPTARG" = x
getopts ":b:" opt
__CPROVER_assert "$opt" = ":"
__CPROVER_assert "$OPTARG" = b
getopts ":b:" opt
__CPROVER_assert "$?" = 1
