#!/bin/sh
n=$RANDOM
__CPROVER_assert "${n}" != "not a number"
