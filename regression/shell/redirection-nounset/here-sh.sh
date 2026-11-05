#!/bin/sh
# With -u, an unset parameter in a redirection: the shell goes on
set -u
unset x
cat <<EOF
$x
EOF
__CPROVER_assert 0 = 1
