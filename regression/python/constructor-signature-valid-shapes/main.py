# Valid constructions must NOT be flagged: correct arity, keyword
# matching a parameter, defaults, **kwargs, and an inherited __init__.
class C:
    def __init__(self, a, b=2):
        self.x = a + b


class K:
    def __init__(self, a, **kw):
        self.a = a


class B:
    def __init__(self, n):
        self.n = n


class D(B):
    pass


c1 = C(1, 2)
c2 = C(1)
c3 = C(a=5, b=6)
k = K(1, extra=9)
d = D(7)
