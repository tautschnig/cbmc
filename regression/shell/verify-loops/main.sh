#!/bin/sh
i=0
s=
while [ "$i" -lt 5 ]; do
  i=$((i + 1))
  if [ "$i" -eq 2 ]; then continue; fi
  if [ "$i" -eq 4 ]; then break; fi
  s="$s$i"
done
__CPROVER_assert "$i" -eq 4
__CPROVER_assert "$s" = 13
for w in a b c; do
  case "$w" in
    a) r=A ;;
    b) continue ;;
    *) r="${r}C" ;;
  esac
done
__CPROVER_assert "$r" = AC
until [ "$i" -le 0 ]; do i=$((i - 2)); done
__CPROVER_assert "$i" -eq 0
