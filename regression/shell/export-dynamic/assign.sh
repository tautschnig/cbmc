#!/bin/sh
# export "NAME=value" assigns; a name that is not known is reported.
export "D=x y"
__CPROVER_assert "$D" = "x y"
if [ "$1" = a ]; then
  export "PREFIX_$2"=v
fi
