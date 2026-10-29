#!/bin/mksh
x=$((010 + 08))
__CPROVER_assert "$x" -eq 18
