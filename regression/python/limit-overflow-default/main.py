# PLR §3.2: Python ints have unlimited precision
# With default 64-bit ints, overflow checks fire on nondet values
x: int = nondet_int()
y: int = x + 1
assert y == x + 1
