# twin (the rebinding hazard that made by-value identity unsound):
# after rebinding, looking up the NEW object must not hit the OLD
# entry.
class C:
    def __init__(self, n):
        self.n = n


k = C(1)
d = {k: 'a'}
k = C(1)
x = d[k]                 # CPython: KeyError
assert x == 'a'
