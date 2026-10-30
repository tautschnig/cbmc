#!/bin/sh
exec 2>/dev/null
__CPROVER_assert "$?" -eq 0
exec 3>"/tmp/x$1"
__CPROVER_assert reached = maybe
if [ "$1" = run ]; then exec true; fi
__CPROVER_assert "$1" != run
