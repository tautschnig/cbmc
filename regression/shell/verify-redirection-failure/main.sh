#!/bin/sh
f() { x=ran; }
f > "/tmp/log$1"
__CPROVER_assert "$?" -eq 0
: > /tmp/out
__CPROVER_assert "$?" -eq 0
