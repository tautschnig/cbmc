# Valid bound-method call shapes must NOT raise a spurious TypeError:
# correct arity, keyword matching a parameter, *args, **kwargs,
# defaults, an inherited method, a staticmethod and a classmethod.
# (Only the calls are exercised — return-value precision is covered
# elsewhere — so this isolates the call-site signature check.)
class C:
    def m(self, a):
        return a

    def va(self, *a):
        return 0

    def kw(self, **k):
        return 0

    def deflt(self, a, b=2):
        return a

    @staticmethod
    def s(a, b):
        return a

    @classmethod
    def cm(cls, a):
        return a


class D(C):
    pass


c = C()
c.m(1)
c.m(a=7)
c.va(1, 2, 3)
c.kw(x=1, y=2)
c.deflt(1)
C.s(1, 2)
C.cm(5)
D().m(9)
