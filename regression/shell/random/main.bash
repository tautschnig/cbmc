#!/bin/bash
# RANDOM: a number from 0 to 32767 at each expansion.
RANDOM=42
n=$RANDOM
[ "$n" -ge 0 ] && [ "$n" -le 32767 ] || __CPROVER_assert 0 = 1
m=$((RANDOM % 6))
