#!/bin/bash
# With -u, an unset parameter in a redirection: the shell goes on
set -u
unset x
cat > $x
__CPROVER_assert 0 = 1
