#!/bin/sh
x=1 y=two
echo "$x" | tr a-z A-Z | wc -c
true && false || echo fallback
! test -f /etc/passwd && echo missing
sleep 1 &
