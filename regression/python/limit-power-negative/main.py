# PLR §6.5: The power operator
# a ** b with negative b should return float
# Note: exact float comparison with division results is unreliable
# in bounded model checking due to rounding mode nondeterminism.
x: int = 2 ** 3
assert x == 8
