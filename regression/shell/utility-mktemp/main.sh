#!/bin/sh
# The output of mktemp is the name it made: the template with its X's
# replaced by letters and digits, in $TMPDIR or /tmp without a template.
f=$(mktemp /dev/shm/.vigpg-XXXXXXXXXXXX)
case "$f" in
  /dev/shm/.vigpg-????????????|"") ;;
  *) __CPROVER_assert 0 = 1 ;;
esac
trap "rm -f $f" EXIT
g=$(mktemp)
case "$g" in
  */tmp.*|"") ;;
  *) __CPROVER_assert 0 = 2 ;;
esac
# $TMPDIR may have a blank.
d=$(mktemp -d)
trap "rm -rf $d" EXIT
