# Identity SETS: default-equality class elements dedup and compare
# by object identity (pointer-element list-backed model).
class C:
    def __init__(self, n):
        self.n = n


k1 = C(1)
k2 = C(1)
s = {k1, k2, k1}
assert len(s) == 2
assert k1 in s
assert k2 in s
e = C(1)
assert e not in s
