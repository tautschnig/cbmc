#!/bin/sh
# return outside a function or dot script exits, with its operand in
# dash, busybox, mksh and zsh, and with status 2 in bash as sh.
(return 3; __CPROVER_assert 0 = 1)
__CPROVER_assert "$?" = 3 -o "$?" = 2
