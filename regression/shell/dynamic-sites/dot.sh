#!/bin/sh
# The file of a dot command from the environment or the arguments may be
# any file.
if [ -n "$2" ]; then
  . "$HOME/.profile"
else
  . "$1"
fi
