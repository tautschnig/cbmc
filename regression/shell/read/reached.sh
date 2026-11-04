#!/bin/sh
x=old
read x
__CPROVER_assert "$x" = old
