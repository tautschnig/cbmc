#!/bin/sh
set -f
set -- *
__CPROVER_assert "$1" = "*"
set +f
set -- *
__CPROVER_assert "$1" = "*"
