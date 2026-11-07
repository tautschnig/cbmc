#!/bin/sh
# The output of getopt, which eval set -- makes the positional parameters.
options=$(getopt -o hvf: -l help,file: -n example -- "$@") || exit 1
eval set -- "$options"
verbose=0
file=
while true; do
  case "$1" in
    -h|--help) echo usage; exit 0 ;;
    -v) verbose=1; shift ;;
    -f|--file) file=$2; shift 2 ;;
    --) shift; break ;;
    *) __CPROVER_assert false "an option or --"; exit 1 ;;
  esac
done
