#!/bin/sh
echo 'single $x' "double $x ${y}" $1 $# $?
echo $(date) `uname`
