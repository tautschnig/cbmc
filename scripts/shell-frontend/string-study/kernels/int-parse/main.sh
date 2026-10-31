#!/bin/sh
n=$1
if [ "$n" -gt 5 ] 2>/dev/null; then __CPROVER_assert "$n" != 3; fi
