class C:
    def __init__(self):
        self.a = 5


# A conditional-expression return `C() if c else None` must infer Optional (the
# tagged union) so the None branch is preserved. Previously the IfExp was an
# unrecognised return form that defaulted to int and safe_typecast'd None to the
# class type, erasing None: `f(0) is None` was unprovable and a None-guard such
# as `if r is not None:` became dead code (a false-proof vector).
def f(c):
    return C() if c else None


# The None branch is reachable and recognised as None.
assert f(0) is None
# The truthy branch is not None.
assert f(1) is not None
# A None guard is honoured: guarded access is provably safe.
r = f(1)
if r is not None:
    assert r.a == 5
