#!/bin/bash
# "${cmd[@]}" as a command name.
check() {
  __CPROVER_assert "$#" = 3
  __CPROVER_assert "$1$2$3" = "ab c"
  ran=yes
}
cmd=(check a)
"${cmd[@]}" "b " c
__CPROVER_assert "$ran" = yes
GPG=(false --batch)
"${GPG[@]}" --import && __CPROVER_assert 0 = 1
empty=()
"${empty[@]}"
__CPROVER_assert "$?" = 0
