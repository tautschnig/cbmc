#!/bin/sh
false | true
__CPROVER_assert "$?" -eq 0
set -o pipefail
(exit 3) | (exit 4) | true
__CPROVER_assert "$?" -eq 4
true | true
__CPROVER_assert "$?" -eq 0
