# PLR §7.2: a chained assignment `a = b = <mutable>` evaluates the RHS ONCE and
# binds BOTH targets to the SAME object, so a mutation through one is visible
# through the other. convert_assign now materialises the value in the first
# target and aliases the rest to it (pointer + alias_targets). CPython: len(b)
# == 2 after a.append(2), so this `assert len(b) == 1` raises. Expected: FAILED.
a = b = [1]
a.append(2)
assert len(b) == 1
