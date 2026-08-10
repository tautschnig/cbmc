# twin: distinct equal-fielded objects must NOT dedup.
class C:
    def __init__(self, n):
        self.n = n


k1 = C(1)
k2 = C(1)
s = {k1, k2}
assert len(s) == 1
