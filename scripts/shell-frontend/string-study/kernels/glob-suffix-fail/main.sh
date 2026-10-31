#!/bin/sh
f=$1
case $f in
  *.txt) kind=text ;;
  *) kind=other ;;
esac
if [ "$kind" = text ]; then __CPROVER_assert "$f" != notes.txt; fi
