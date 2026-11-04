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
