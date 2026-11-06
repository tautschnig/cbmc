#!/bin/sh
# A part with a blank is reported: the trap would remove two files.
tmp=$1
trap "rm -f $tmp" EXIT
