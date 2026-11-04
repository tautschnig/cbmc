#!/bin/sh
# Known values: a command name with several fields, and a list.
greet() { r="hi $1"; }
cmd="greet you"
$cmd
__CPROVER_assert "$r" = "hi you"
tools="jar javac javadoc"
n=0
for t in $tools; do n=$((n + 1)); done
__CPROVER_assert "$n" = 3
