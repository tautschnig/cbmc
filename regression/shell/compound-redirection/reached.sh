#!/bin/sh
x=0
{ x=1; } > "$1"
__CPROVER_assert "$x" = 1
