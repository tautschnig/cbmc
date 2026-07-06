# No-false-positive: read-before-del, rebind-after-del, a DIFFERENT instance, and
# a class-level attr's del (which falls back to the class value) must NOT raise.
class C:
    def __init__(self):
        self.a = 5


class D:
    k = 7  # class-level default

    def __init__(self):
        self.k = 5


c = C()
r = c.a
del c.a
assert r == 5

c2 = C()
del c2.a
c2.a = 9
assert c2.a == 9

a = C()
b = C()
del a.a
assert b.a == 5

d = D()
del d.k
assert d.k == 7  # falls back to class-level default
