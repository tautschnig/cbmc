#!/bin/sh
# Quoted expansions and unquoted ones that yield one field are matched
# literally.
dir="/usr/l*b"
sub=lib
for f in "$dir"/$sub?.so; do
  case "$f" in
    "/usr/l*b/lib"?.so) ;;
    *) __CPROVER_assert 0 = 1 ;;
  esac
done
