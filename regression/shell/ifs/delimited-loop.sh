#!/bin/sh
# No field has a byte of IFS.
IFS=:
x="$1"
case "$x" in
  *[*?[]*) ;;
  ?|??|???)
    for f in $x; do
      case "$f" in *:*) __CPROVER_assert 0 = 1 ;; esac
    done
    ;;
esac
