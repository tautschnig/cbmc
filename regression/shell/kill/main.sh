#!/bin/sh
# kill: signals to other processes; kill -0 and kill -l send none.
pid=1234
case "$$" in 1234) exit ;; esac
kill "$pid" || :
kill -s TERM $pid
kill -0 "$$"
kill -l > /dev/null
