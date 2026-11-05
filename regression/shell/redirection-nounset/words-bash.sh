#!/bin/bash
# With -u, unset parameters in both the words and a redirection: either
# makes the shell exit, or the redirection fails in the child process.
set -u
unset x
y=file
cat $y > $x
__CPROVER_assert 0 = 1
