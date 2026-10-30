#!/bin/sh
if [ "$1" = a ]; then
  cd() { :; }
fi
cd /tmp
