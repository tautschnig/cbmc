#!/bin/sh
# dash and busybox do not set UID or EUID; bash run as sh does.
if [ "$EUID" = 0 ]; then echo root; fi
case "$UID" in "") __CPROVER_assert 0 = 1 ;; esac
