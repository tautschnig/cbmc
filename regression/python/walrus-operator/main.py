# PLR §6.12: Assignment expressions (':=', a.k.a. walrus operator).
# The expression evaluates to its RHS and, as a side effect, binds
# the RHS to the target name in the enclosing scope.

# Used directly in a condition; target visible after the if.
if (n := 10) > 5:
    assert n == 10

# Re-bind later; both paths see the same target.
if (m := 1) > 0:
    assert m == 1
m = m + 1
assert m == 2


# Inside a function, binding a fresh local.
def count_positive(a: int, b: int) -> int:
    total: int = 0
    if (s := a + b) > 0:
        total = s
    return total


assert count_positive(3, 4) == 7
assert count_positive(-1, -2) == 0
