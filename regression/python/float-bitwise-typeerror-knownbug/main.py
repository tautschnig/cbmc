# KNOWNBUG (false proof): bitwise/shift/invert operators require integral
# operands; a float operand raises TypeError in CPython (1.0 & 2 ->
# "unsupported operand type(s) for &: 'float' and 'int'"). cbmc silently
# computes a value and reaches the assert.
x = 1.0 & 2
assert True      # unreachable in CPython
