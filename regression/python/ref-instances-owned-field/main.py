# Phase-3 OWNED-field identity: a constructed field
# (self.x = Inner(1)) heap-allocates PER EXECUTION of the store --
# two Holder() instances own DISTINCT Inner objects, while two
# reads of one holder's field give the SAME object (PLR 3.1).
# Covers plain, annotated, mixed by-ref/owned, and rebinding.
class Inner:
    def __init__(self, v: int) -> None:
        self.v = v


class Holder:
    def __init__(self, shared: Inner) -> None:
        self.s = shared
        self.o = Inner(2)

    def get(self) -> Inner:
        return self.o


i = Inner(1)
h1 = Holder(i)
h2 = Holder(i)
assert h1.get() is h1.get()
assert h1.o is not h2.o
assert h1.s is i and h2.s is i
h1.o.v = 7
assert h2.o.v == 2
assert i.v == 1
old = h1.o
h1.o = Inner(9)
assert h1.o is not old
assert old.v == 7
