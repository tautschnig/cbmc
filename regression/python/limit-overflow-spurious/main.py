x: int = nondet_int()
y: int = nondet_int()
z: int = x + y
assert z == x + y
