# KNOWNBUG (false proof, reference-semantics-for-instances): an instance flowed
# through a RETURN (`u = f(v)` where f returns its parameter) is value-COPIED, so
# `u` and `v` are not the same object and a mutation through `u` is invisible to
# `v`. CPython: f returns the SAME object, so v.x is 99 after u.x = 99 -> the
# assert is false. The return is a copy site that loses identity.
class V:
    def __init__(self) -> None:
        self.x: int = 1


def f(t: V) -> V:
    return t


v = V()
u = f(v)
u.x = 99
assert v.x == 1   # FALSE in CPython (u is v; v.x is 99)
