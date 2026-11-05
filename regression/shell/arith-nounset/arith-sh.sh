#!/bin/sh
set -u
unset y z
: $((y += 1))
__CPROVER_assert 0 = 1
