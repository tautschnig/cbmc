# PLR §3.2: Python ints have unlimited precision
# With 64-bit ints, nondet arithmetic can overflow
x: int = nondet_int()
y: int = nondet_int()
z: int = x + y  # may overflow
assert z == x + y  # tautology but overflow check fails
