# Callee-side pointer return: a method returning a by-reference
# field (`return self.x`, the Phase-3 pointer field) hands back the
# object's IDENTITY -- the return slot is pointer-typed, so `is`,
# shared mutation, chained attribute reads, and arg passing all see
# the one object (PLR 3.1: assignment and return never copy).
class Inner:
    def __init__(self, v: int) -> None:
        self.v = v


class Holder:
    def __init__(self, x: Inner) -> None:
        self.x = x

    def get(self) -> Inner:
        return self.x


def probe(t: Inner) -> int:
    return t.v


i = Inner(3)
h = Holder(i)
g = h.get()
assert g is i
assert h.get() is i
assert h.get().v == 3
h.get().v = 8
assert i.v == 8
assert probe(h.get()) == 8
