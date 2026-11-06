#!/bin/bash
# bash allows a slash in a function name, and looks functions up first.
reached=no
d/run() {
  reached=yes
}
if [ -n "$1" ]; then
  p=d
else
  p="$2"
fi
"$p/run"
if [ -n "$1" ]; then
  __CPROVER_assert "$reached" = yes
fi
