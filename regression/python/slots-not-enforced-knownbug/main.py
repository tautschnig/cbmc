# KNOWNBUG: __slots__ restricts a class's attributes; assigning an attribute not
# in __slots__ raises AttributeError in CPython. The frontend does not model
# __slots__, so `c.b = 5` is accepted. Desired: VERIFICATION FAILED.
class C:
    __slots__ = ("a",)


c = C()
c.b = 5
