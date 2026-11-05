#!/bin/sh
# Assignments before a function call last for the call.
f() {
  __CPROVER_assert "$V" = tmp
  V=set_in_f
}
V=old
V=tmp f
__CPROVER_assert "$V" = old
unset V
V=tmp f
__CPROVER_assert "${V-unset}" = unset
