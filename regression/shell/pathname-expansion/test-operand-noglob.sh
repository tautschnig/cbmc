#!/bin/sh
set -f
[ /etc/*.conf = "/etc/*.conf" ]
__CPROVER_assert $? = 0
