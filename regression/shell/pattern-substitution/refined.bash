#!/bin/bash
# Pattern substitution with refined strings.
x=abcb
__CPROVER_assert "${x/b/X}" = aXcb
__CPROVER_assert "${x/#a/X}" = Xbcb
__CPROVER_assert "${x/%b/}" = abc
