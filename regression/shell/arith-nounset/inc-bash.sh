#!/bin/bash
set -u
unset z
: $((z++))
__CPROVER_assert 0 = 1
