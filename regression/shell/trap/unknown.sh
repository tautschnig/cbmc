#!/bin/sh
tmp=$(mktemp)
trap "rm -f $tmp" EXIT
