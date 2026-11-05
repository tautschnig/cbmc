#!/bin/bash
# Without names, read assigns REPLY.
REPLY=pre
if read -r; then
  case "$REPLY" in *"
"*) __CPROVER_assert 0 = 1 ;; esac
fi
x=$REPLY
