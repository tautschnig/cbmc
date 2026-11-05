#!/bin/sh
f() { r=function; }
command -v f > /dev/null
__CPROVER_assert "$?" = 0
command -v cd > /dev/null
__CPROVER_assert "$?" = 0
if command -v dpkg-maintscript-helper > /dev/null 2>&1; then
  found=yes
else
  __CPROVER_assert "$?" = 127 -o "$?" = 1
fi
r=none
command f 2> /dev/null
__CPROVER_assert "$r" = none
command true
__CPROVER_assert "$?" = 0
name=f
command -v "$name" > /dev/null
__CPROVER_assert "$?" = 0
# Assignments before command -v do not persist.
unset X
X=1 PATH= command -v f > /dev/null
__CPROVER_assert "$?" = 0
__CPROVER_assert "${X-unset}" = unset
