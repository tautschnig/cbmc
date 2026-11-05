#!/bin/sh
# ${#name} in arithmetic is the length of the value, a decimal number;
# ${#} is $#.
x=abc
set -- hello
n=$(( (${#1} - ${#x}) * 2 ))
__CPROVER_assert "$n" = 4
m=$(( ${#} + 1 ))
__CPROVER_assert "$m" = 2
