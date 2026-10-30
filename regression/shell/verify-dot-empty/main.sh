#!/bin/sh
false
. /dev/null
__CPROVER_assert "$?" -eq 0
