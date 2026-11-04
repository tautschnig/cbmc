#!/bin/sh
command -v gzip > /dev/null
__CPROVER_assert "$?" = 0
