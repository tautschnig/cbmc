#!/bin/sh
tool=$1
if [ -n "$2" ]; then
  out=">/dev/null"
else
  out=">&2"
fi
eval "echo installing $tool $out"
eval "value=\"$tool\""
__CPROVER_assert "$value" = "$tool"
