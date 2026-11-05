#!/bin/bash
# &>word is >word 2>&1, and &>>word is >>word 2>&1.
x=0
{ x=1; } &> /dev/null
__CPROVER_assert "$x" = 1
if ls &>> "$1"; then :; fi
