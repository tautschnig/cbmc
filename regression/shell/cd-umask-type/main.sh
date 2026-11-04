#!/bin/sh
# cd, umask and type: the file system is not modelled.
x=kept
if cd "$1"; then
  case "$PWD" in /*) ;; *) __CPROVER_assert 0 = 1 ;; esac
else
  __CPROVER_assert "$?" != 0
fi
__CPROVER_assert "$x" = kept
umask 022
__CPROVER_assert "$?" = 0
umask u=rwx,go=rx
__CPROVER_assert "$?" = 0
type nosuch-xyz > /dev/null 2>&1
__CPROVER_assert "$?" != 0
