#!/bin/sh
# A command name from the environment may be any string; a site that the
# script cannot reach has no candidates.
cmd=${CMD:-true}
$cmd
exit 0
$cmd
