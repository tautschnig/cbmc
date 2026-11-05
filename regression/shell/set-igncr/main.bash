#!/bin/bash
# bash has the option igncr on Cygwin only; elsewhere set -o igncr fails
# with status 2.
(set -o igncr) 2>/dev/null && set -o igncr
__CPROVER_assert "$?" = 2
set -o igncr
__CPROVER_assert "$?" = 2
