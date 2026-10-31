#!/bin/sh
x=$1
if [ -z "$x" ]; then y=default; else y=$x; fi
__CPROVER_assert "$y" != x
