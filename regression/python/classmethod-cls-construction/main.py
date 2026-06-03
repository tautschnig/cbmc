# PLR §9.3: inside a classmethod, `cls(...)` constructs an instance of
# the enclosing class. Both the return type (no annotation) and the
# constructed value must be modelled: the result is the class, and the
# constructor arguments propagate to the instance's fields.
class Point:
    x: int
    y: int

    def __init__(self, x: int = 0, y: int = 0):
        self.x = x
        self.y = y

    @classmethod
    def origin(cls):
        return cls()

    @classmethod
    def of(cls, a: int, b: int):
        return cls(a, b)


o = Point.origin()
assert o.x == 0
assert o.y == 0

p = Point.of(3, 4)
assert p.x == 3
assert p.y == 4
