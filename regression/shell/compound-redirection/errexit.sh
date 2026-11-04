#!/bin/sh
set -e
{ :; } > "$1"
__CPROVER_assert "$?" = 0
