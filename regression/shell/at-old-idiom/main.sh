#!/bin/sh
set -- x ${1+"$@"}
__CPROVER_assert "$1" = x
