# CORE no-false-alarm: a correctly-typed init store keeps the concrete field
# type (widening fires only for a mismatched/uninferable store), so int
# operations on the field remain precise.
class C:
    def __init__(self, v: int) -> None:
        self.x: int = v


c = C(5)
assert isinstance(c.x, int)
assert c.x + 1 == 6
