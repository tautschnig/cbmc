#!/bin/sh
dir=/dev
false
. "$dir/null"
__CPROVER_assert "$?" -eq 0
missing=/nonexistent/cbmc-shell-test.sh
if [ -r "$missing" ]; then . "$missing"; fi
case "$1" in
  a) f=/dev/null ;;
  *) f=/nonexistent/cbmc-shell-test.sh ;;
esac
. "$f"
