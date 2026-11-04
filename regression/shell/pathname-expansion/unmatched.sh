#!/bin/sh
for f in /nonexistent/*.x; do
  __CPROVER_assert "$f" != "/nonexistent/*.x"
done
