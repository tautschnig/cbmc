#!/bin/sh
# Redirections without a command name.
x=$(exit 3) 2>/dev/null
__CPROVER_assert "$?" = 3
x=1 >/dev/null
__CPROVER_assert "$x" = 1
if > "$1"; then
  __CPROVER_assert "$?" = 0
else
  __CPROVER_assert "$?" != 0
fi
: <<\DOC_END
documentation
DOC_END
<<\EOF2
also documentation
EOF2
