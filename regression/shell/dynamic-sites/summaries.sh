#!/bin/sh
# Variables assigned in a function before every site that follows a call
# to that function have only the values assigned.
setup() {
  pager=cat
}
setup
$pager x
# A saved copy that is restored keeps the assigned status of the
# variable where it was saved.
saved=$pager
pager=echo
$pager y
pager=$saved
$pager z
