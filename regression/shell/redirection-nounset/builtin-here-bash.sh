#!/bin/bash
# With -u, an unset parameter in a redirection: the shell exits
set -u
unset x
read v <<EOF
$x
EOF
__CPROVER_assert 0 = 1
