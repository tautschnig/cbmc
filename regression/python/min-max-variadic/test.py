# Regression: min/max with more than 2 numeric arguments compute the
# correct value. Previously only 2-arg forms folded; 3+ args fell
# through to a generic call that returned a nondet value.

# Variadic numeric: min(3, 2.5, 1) = 1.0, not 2.5.
assert min(3, 2.5, 1) == 1
assert max(3, 2.5, 1) == 3

# Mixed int/float: pickled into a double, max returns the largest.
assert max(1, 2, 3) == 3
assert min(1, 2, 3) == 1

# Inlined heterogeneous list — element python_value tagged-union
# fields are unwrapped from the constant operands.
assert max([1, 2.5, 3]) == 3
assert min([1, 2.5, 3]) == 1
