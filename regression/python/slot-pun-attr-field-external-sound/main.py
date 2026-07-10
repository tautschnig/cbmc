# CORE (2026-07-10): an EXTERNAL store `o.x = <mismatch>` to a correctly-typed
# field (`self.x: int = 0`) no longer PUNS the value into int. A module-wide scan
# (keyed on the attribute NAME, a sound over-approximation like the del +
# __getattr__ scan) detects an external store whose scalar CATEGORY differs from
# the declared field type -- here `o.x = src()` where src returns a str -- and
# widens the field to python_value at class-definition time, preserving the tag.
# CPython: c.x is "x" (str), isinstance("x", int) is False -> AssertionError, so
# the correct result is VERIFICATION FAILED. This closes the LAST slot-pun; the
# concrete-slot-punning whole-group (list-element all store forms + attribute
# field init AND external stores) is now fully closed.
def src():
    return "x"


class C:
    def __init__(self) -> None:
        self.x: int = 0


c = C()
c.x = src()
assert isinstance(c.x, int)
