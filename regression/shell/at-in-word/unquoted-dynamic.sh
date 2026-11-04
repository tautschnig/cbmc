#!/bin/sh
for i in $@; do
  case "$i" in *" "* | "") __CPROVER_assert 0 = 1 ;; esac
done
