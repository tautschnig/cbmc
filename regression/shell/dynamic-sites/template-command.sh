#!/bin/sh
# A hole in the command name of eval text makes it a dynamic command name,
# which here runs a utility.
dir=$1
eval "$dir/msg hello"
