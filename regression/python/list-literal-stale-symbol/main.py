# PLR §3.1: a cached list built from a variable (`xs = xs + [b]`, so xs holds b's
# value 0) must NOT go stale when that variable is reassigned (`b = 9`). Reusing
# a cached struct that references the symbol b would read the NEW b -> false
# proof. The cache is invalidated on reassignment, so r is [8,5,0,3] and
# `r != [8,5,0,3]` raises. Found by the mutation-oracle. CPython: AssertionError.
b = 0
xs = [8, 5]
xs = xs + [b]
b = 9
r = xs + [3]
assert r != [8, 5, 0, 3]
