#!/bin/sh
if [ -n "$1" ]; then
  setting="mode=fast"
else
  setting="mode=slow"
fi
eval "$setting"
__CPROVER_assert "$mode" = fast -o "$mode" = slow
