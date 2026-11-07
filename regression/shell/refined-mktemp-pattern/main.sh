#!/bin/sh
g=$(mktemp)
case "$g" in
  */tmp.*|"") ;;
  *) __CPROVER_assert 0 = 1 ;;
esac
