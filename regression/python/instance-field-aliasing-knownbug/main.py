# KNOWNBUG (false proof, reference-semantics-for-instances): a class instance
# stored into a concrete-class-typed FIELD (`self.t = t`) is value-COPIED, so a
# later mutation through the holder (h.m() -> self.t.x = 99) is invisible to the
# original object. CPython: v.x is 99 after h.m() -> the assert is false.
# Concrete-class PARAMS are already by-reference; the field store is the copy
# site that loses identity. Closes together with the local-alias and
# return-flow cases under a unified instance-reference-semantics fix.
class V:
    def __init__(self) -> None:
        self.x: int = 1


class H:
    def __init__(self, t: V) -> None:
        self.t = t

    def m(self) -> None:
        self.t.x = 99


v = V()
h = H(v)
h.m()
assert v.x == 1   # FALSE in CPython (v.x is 99)
