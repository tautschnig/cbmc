#!/bin/sh
# type with a quoted expansion: one name, which may or may not be found,
# and must not start with - (shells differ in their options).
f_x() { :; }
type "f_$1" > /dev/null 2>&1
__CPROVER_assert "$?" -le 255
type "$1" > /dev/null 2>&1
