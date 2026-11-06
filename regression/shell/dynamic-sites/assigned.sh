#!/bin/sh
# A variable that the script assigns on every path before the site has
# only the values it assigns, although it is also in the environment.
if [ -n "$1" ]; then
  tool=cat
else
  tool=echo
fi
$tool x
case "$1" in
  a) other=true ;;
esac
$other
