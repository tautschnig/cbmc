#!/bin/bash
set --
__CPROVER_assert "${*-unset}" = unset
set -- a bc
__CPROVER_assert "${#*}" = 2
