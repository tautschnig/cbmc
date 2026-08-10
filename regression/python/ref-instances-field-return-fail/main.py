# Negative twin: the returned field IS the stored object, so
# distinctness must be REFUTED (guards the identity claim against
# a nondet-`is` false model).
class Inner:
    def __init__(self, v: int) -> None:
        self.v = v


class Holder:
    def __init__(self, x: Inner) -> None:
        self.x = x

    def get(self) -> Inner:
        return self.x


i = Inner(3)
h = Holder(i)
assert h.get() is not i
