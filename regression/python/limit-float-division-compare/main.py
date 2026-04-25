# PLR §6.7: true division always returns float
# CBMC's floatbv_div with nondet rounding mode can't prove equality
x: float = 10.0 / 3.0
assert x > 3.0
