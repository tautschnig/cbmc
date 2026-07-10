# PLR §3.1: a cached tuple built from a variable (`t = (b, a)`, capturing b=2)
# must NOT go stale when b is reassigned (b=5). A fold reusing the cached struct
# that references symbol b would read the NEW b -> false proof. tuple_literals
# is now invalidated on reassignment (same whole-group as list_literals). r is
# (2,0), so `r != (2,0)` raises. Found by the mutation-oracle.
b = 2
a = 0
t = (b, a)
b = 5
r = t * 1
assert r != (2, 0)
