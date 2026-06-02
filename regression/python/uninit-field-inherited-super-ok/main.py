# Companion: when the subclass DOES call super().__init__(), the
# inherited field is assigned through the base initializer, so reading
# it must NOT raise AttributeError.
class Base:
    x: int

    def __init__(self, v: int) -> None:
        self.x = v


class Sub(Base):
    def __init__(self, v: int) -> None:
        super().__init__(v)
        self.y = v * 2


s = Sub(5)
assert s.x == 5
assert s.y == 10
