# No-false-positive guard for the __slots__ read check: a slot, a method, a
# class-level attr, an inherited method, and an object dunder are all readable
# on a slots-enforced instance and must NOT be flagged. A __dict__-backed base
# (B has no __slots__) disables enforcement entirely.
class B:
    __slots__ = ("a",)

    def m(self) -> int:
        return 7


class C(B):
    __slots__ = ("b",)
    z = 9


c = C()
c.a = 1
c.b = 2
assert c.a == 1
assert c.m() == 7
assert c.z == 9
# Reading an object dunder must not raise AttributeError (its value is
# over-approximated to nondet, so it is read but not asserted).
t = c.__class__
