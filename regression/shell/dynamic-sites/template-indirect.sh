#!/bin/sh
kopt_root=/dev/sda1
kopt_quiet=yes
get() {
  eval "value=\${kopt_$1}"
}
get root
r=$value
get "$2"
__CPROVER_assert "$r" = /dev/sda1
set_opt() {
  eval "$1=on"
}
set_opt flag
__CPROVER_assert "$flag" = on
