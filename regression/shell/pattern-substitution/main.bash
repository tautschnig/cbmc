#!/bin/bash
# ${x/pattern/string}: first, all, leading and trailing matches.
x=abcb
__CPROVER_assert "${x/b/X}" = aXcb
__CPROVER_assert "${x//b/X}" = aXcX
__CPROVER_assert "${x/#a/X}" = Xbcb
__CPROVER_assert "${x/#b/X}" = abcb
__CPROVER_assert "${x/%b/X}" = abcX
__CPROVER_assert "${x/#/P}" = Pabcb
__CPROVER_assert "${x/b}" = acb
__CPROVER_assert "${x/#a*c/}" = b
y=aaa
__CPROVER_assert "${y//aa/X}" = Xa
dir=/dev
v=/dev/sda
__CPROVER_assert "${v/#"$dir/"/}" = sda
