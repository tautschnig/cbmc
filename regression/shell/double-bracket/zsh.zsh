#!/bin/zsh
p="a*"
[[ abc == $p ]] && __CPROVER_assert 0 = 1
