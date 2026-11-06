#!/bin/sh
if [ -n "$1" ]; then
  action="exit 3"
else
  action=:
fi
trap "$action" EXIT
