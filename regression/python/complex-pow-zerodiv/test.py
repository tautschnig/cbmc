# Regression: complex(0+0j) ** <negative> must raise ZeroDivisionError.
# Previously the frontend produced a nondet result and the bug was
# silently swallowed.
z = complex(0, 0) ** (-2)
