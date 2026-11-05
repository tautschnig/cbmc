#!/bin/bash
x=0
eval "x=1" 2> "$1"
__CPROVER_assert "$x" = 1
