#!/bin/sh
z=" a b "
set -- $z
__CPROVER_assert "$#" = 2
__CPROVER_assert "$2" = b
