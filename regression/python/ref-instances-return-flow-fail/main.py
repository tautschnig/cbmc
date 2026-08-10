# twin: factory results are DISTINCT objects -- claiming identity
# must fail.
class C:
    def __init__(self, n):
        self.n = n


def make(n: int) -> C:
    return C(n)


k1 = make(1)
k2 = make(1)
assert k1 is k2
