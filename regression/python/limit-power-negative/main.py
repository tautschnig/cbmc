# PLR §6.5: The power operator
# a ** b with negative b should return float
x: float = 2 ** -1
assert x == 0.5
