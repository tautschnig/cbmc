#!/bin/sh
# ~ and ~/path expand to HOME.
HOME="/home/a b"
x=~
__CPROVER_assert "$x" = "/home/a b"
set -- ~/.config a~
__CPROVER_assert "$#" = 2
__CPROVER_assert "$1" = "/home/a b/.config"
__CPROVER_assert "$2" = "a~"
__CPROVER_assert ~ = "/home/a b"
