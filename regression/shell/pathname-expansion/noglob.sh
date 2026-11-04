#!/bin/sh
set -f
for f in /etc/*; do __CPROVER_assert "$f" = "/etc/*"; done
