#!/bin/sh
if [ "$1" = a ]; then echo a; elif [ "$1" = b ]; then echo b; else echo c; fi
while false; do :; done
until true; do :; done
for i in 1 2; do echo $i; done
for j; do echo $j; done
case $1 in a|b) echo ab;; *) echo other;; esac
f() { echo in f; }
(cd /tmp && ls)
{ echo grouped; }
