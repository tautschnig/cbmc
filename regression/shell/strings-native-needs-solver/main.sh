#!/bin/sh
# Bounded strings of 24 bytes would fail shell-string-length here.
x=$1
y="abcdefghijklmnopqrstuvwxyz$x"
__CPROVER_assert "${#y}" -ge 26
case $y in abc*) ;; *) __CPROVER_assert 1 = 0 ;; esac
