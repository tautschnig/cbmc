#!/bin/sh
x=$1
y="pre${x}post"
if [ "$y" = prefoopost ]; then __CPROVER_assert "$x" = foo; fi
