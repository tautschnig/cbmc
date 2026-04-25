# ESBMC-specific: assume() constrains nondet values
x: int = nondet_int()
assume(x > 0)
assume(x < 10)
assert x >= 1
