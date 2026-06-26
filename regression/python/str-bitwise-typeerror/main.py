# PLR §6.7: bitwise/shift operators require integral operands; a str operand
# raises TypeError ("unsupported operand type(s) for &: 'str' and 'int'"). str,
# like float, is unambiguously not a set, so this is a definite TypeError (the
# set-ambiguous general bitwise case stays silent).
x = "a" & 1
assert True   # unreachable in CPython
