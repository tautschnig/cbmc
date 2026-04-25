# PLR §3.2: "Integers have unlimited precision"
# With bounded 64-bit ints, x+1 can overflow. Use --python-unbounded-ints.
x: int = nondet_int()
y: int = x + 1
assert x < y
