#!/bin/sh
# dash and busybox assign UID; bash run as sh exits, as it is read-only.
if [ -z "$UID" ]; then
  UID=1000
fi
__CPROVER_assert -n "$UID"
EUID=0
__CPROVER_assert "$EUID" = 0
