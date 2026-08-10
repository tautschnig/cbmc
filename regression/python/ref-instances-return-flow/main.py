# Return-flow identity: a PROVABLY-FRESH factory (every return a
# direct constructor call, no fall-through) re-boxes its result into
# heap identity storage at the caller -- factory-made objects get
# full identity semantics (distinct keys, is/is-not, dict identity).
class C:
    def __init__(self, n):
        self.n = n


def make(n: int) -> C:
    return C(n)


k1 = make(1)
k2 = make(1)
assert k1 is not k2
d = {k1: 'a', k2: 'b'}
assert len(d) == 2
assert d[k1] == 'a'
assert d[k2] == 'b'
