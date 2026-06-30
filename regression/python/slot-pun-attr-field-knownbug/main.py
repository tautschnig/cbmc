# KNOWNBUG (concrete-slot punning, annotation-laundering class; master inventory
# section A "Coercion-boundary audit", row "Attribute field"; the a2/a3
# field-narrowing witnesses). A field declared with a CONCRETE type (`self.x: int`)
# puns an Any/`python_value` of a different runtime tag stored into it (here a str
# from an unannotated `src()`), dropping the tag. CPython: `c.x` is "x" (str),
# `isinstance("x", int)` is False -> AssertionError. cbmc proves the assertion
# (false proof). Desired: VERIFICATION FAILED. Fix = slot-widening (type the field
# `python_value` when one is stored), invasive + perf-costly, deferred. The
# distinct *tagged-union* field case (`self.x: int | str`) IS now caught
# (tagged-union-narrowing-unsound, CORE), as is composition aliasing
# (shared-object-aliasing, CORE) -- only the CONCRETE-typed field puns.
def src():
    return "x"


class C:
    def __init__(self) -> None:
        self.x: int = 0


c = C()
c.x = src()
assert isinstance(c.x, int)
