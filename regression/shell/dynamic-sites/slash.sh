#!/bin/sh
# A command name with a slash names a utility (XCU 2.9.1.4), whatever the
# directory is.
dir=$1
"$dir/tool" x
if [ -n "$2" ]; then
  pager=/usr/bin/less
else
  pager="$dir/more"
fi
"$pager" y
