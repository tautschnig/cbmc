#!/bin/sh
# Pathname expansion in lists of fields: a pattern yields matching
# pathnames, or itself where none matches.
for f in /etc/*.conf; do
  case "$f" in
    /etc/*.conf) ;;
    *) __CPROVER_assert 0 = 1 ;;
  esac
done
for f in /nonexistent/\*; do __CPROVER_assert "$f" = "/nonexistent/*"; done
