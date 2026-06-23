# PLR 9.3.4: an unbound method call Class.method(inst, ...) passes the
# instance explicitly as the first positional arg (self).
class C:
    def __init__(self, x: int):
        self.x = x

    def get(self) -> int:
        return self.x

    def add(self, y: int) -> int:
        return self.x + y


i = C(5)
assert C.get(i) == 5          # positional self
assert C.get(self=i) == 5     # keyword self
assert i.get() == 5           # bound
assert C.add(i, 3) == 8       # positional self + extra arg
