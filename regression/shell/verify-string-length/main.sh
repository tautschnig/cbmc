#!/bin/sh
x="$1$1"
__CPROVER_assert "${#1}" -le 30
__CPROVER_assert "${#x}" -le 60
__CPROVER_assert "${#x}" -le 30
