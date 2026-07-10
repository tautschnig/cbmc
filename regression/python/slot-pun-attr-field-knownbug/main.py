# KNOWNBUG (concrete-slot punning, annotation-laundering class; master inventory
# section A "Coercion-boundary audit", row "Attribute field"). A field declared
# with a CONCRETE type (`self.x: int`) puns an Any/`python_value` of a different
# runtime tag stored into it. This test exercises the REMAINING open case: the
# field is correctly initialised (`self.x: int = 0`, a compatible literal, so the
# field stays int) and then an EXTERNAL store `c.x = src()` (src returns str)
# puns the str into int. CPython: `c.x` is "x" (str), `isinstance("x", int)` is
# False -> AssertionError. cbmc proves the assertion (false proof). Desired:
# VERIFICATION FAILED.
#
# NOTE (2026-07-10): the INIT-store sub-case (`self.x: int = v` with an
# uninferable/mismatched v) is now SOUND -- the field-typing scan widens the
# field to python_value (slot-pun-attr-field-init-sound / -nofp CORE), which also
# fixes external stores to such a widened field. The remaining hole is an
# external store to a field that was CORRECTLY initialised (below): closing it
# needs external-store scanning with receiver-class resolution, or slot-widening.
# The distinct *tagged-union* field case (`self.x: int | str`) IS caught
# (tagged-union-narrowing-unsound, CORE), as is composition aliasing
# (shared-object-aliasing, CORE).
def src():
    return "x"


class C:
    def __init__(self) -> None:
        self.x: int = 0


c = C()
c.x = src()
assert isinstance(c.x, int)
