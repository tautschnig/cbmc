#!/bin/sh
pager=cat
# g is not defined yet: the shell does not find it, and does not call it.
g a b
g() {
  pager=$2
}
$pager x
