# CORE no-false-alarm: a correctly-typed external store keeps the concrete field
# type (widening fires only for a mismatched-scalar external store), so int
# operations stay precise. An int-returning call also keeps it precise.
class C:
    def __init__(self) -> None:
        self.x: int = 0


def geti() -> int:
    return 7


o = C()
o.x = 5
o.x = geti()
assert o.x + 1 == 8
