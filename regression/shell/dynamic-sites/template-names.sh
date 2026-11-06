#!/bin/sh
# A template matches the function do_run, which gets a branch; other names
# that it matches, such as kde4-config, run a utility.
ran=no
do_run() {
  ran=yes
}
if [ -n "$1" ]; then
  prefix=do
else
  prefix=$2
fi
"${prefix}_run"
if [ -n "$1" ]; then
  __CPROVER_assert "$ran" = yes
fi
"kde${3}-config" --path mime
