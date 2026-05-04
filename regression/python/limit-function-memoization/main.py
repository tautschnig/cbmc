# f(x) == f(x) fails because each call returns independent nondet
import math
x: int = 5
k: int = 2
assert math.comb(x, k) == math.comb(x, k)
