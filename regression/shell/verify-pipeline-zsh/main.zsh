#!/bin/zsh
v=1
true | { v=2; }
__CPROVER_assert "$v" -eq 2
