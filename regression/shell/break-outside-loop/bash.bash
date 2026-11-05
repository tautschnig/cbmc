#!/bin/bash
f() { break; }
n=0
for i in 1 2; do f; n=$((n + 1)); done
__CPROVER_assert "$n" = 2
