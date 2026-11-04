#!/bin/sh
# A bracket expression between stars matches as its characters do.
case "$4" in *[0-3]*) a=1 ;; *) a=0 ;; esac
case "$4" in *0* | *1* | *2* | *3*) b=1 ;; *) b=0 ;; esac
__CPROVER_assert "$a" = "$b"

# ${1%%[!0-9]*} is the longest prefix of digits. (The round trips through
# the removals of main.sh take refined strings long.)
k=${1%%[!0-9]*}
case $k in *[!0-9]*) __CPROVER_assert 0 = 1 ;; esac
case "${1#"$k"}" in "" | [!0-9]*) ;; *) __CPROVER_assert 0 = 1 ;; esac

# ${2##*[0-9]} is the longest suffix without digits.
t=${2##*[0-9]}
case $t in *[0-9]*) __CPROVER_assert 0 = 1 ;; esac

# Removing a prefix of a quoted expansion and a ?.
case "$3" in
  "$5"?*) __CPROVER_assert "${3#"$5"?}" != "$3" ;;
  *) __CPROVER_assert "${3#"$5"?}" = "$3" ;;
esac
