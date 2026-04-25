# PLR: nondet values should support arithmetic verification
x: int = nondet_int()
y: int = x + 1
assert x < y
