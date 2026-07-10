# CORE (2026-07-10): `self.x: int = v` where v's type is uninferable (an
# UNANNOTATED param -> Any) no longer PUNS the stored value into int. The
# field-typing scan widens the field to python_value when the init store is
# uninferable/incompatible with the declared scalar type, so the tag is
# preserved. src() returns a str; CPython: c.x is "x", isinstance("x", int) is
# False -> AssertionError, so the correct result is VERIFICATION FAILED.
def src():
    return "x"


class C:
    def __init__(self, v) -> None:
        self.x: int = v


c = C(src())
assert isinstance(c.x, int)
