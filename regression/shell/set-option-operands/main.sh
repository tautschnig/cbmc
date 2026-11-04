#!/bin/sh
# Words after the options of set are positional parameters, which enable
# no option; each o takes the next word as an option name.
unset nope
set -- -u
__CPROVER_assert "$1" = -u
x=$nope
set -eo pipefail -u
y=$nope
__CPROVER_assert 0 = 1
