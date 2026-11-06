#!/bin/sh
if [ -n "$1" ]; then
  action="done=yes"
else
  action=:
fi
trap "$action" EXIT
tmp=$(mktemp)
trap "rm -f $tmp" EXIT
