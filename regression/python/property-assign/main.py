# @property assignment binding: v = c.prop now
# emits the CALL and the ASSIGN (PLR 3.3.2).


class Circle:
    def __init__(self, r: int) -> None:
        self.r = r

    @property
    def diameter(self) -> int:
        return self.r * 2

    @property
    def always42(self) -> int:
        return 42


c = Circle(5)

# v = c.prop binds the computed value.
v = c.diameter
assert v == 10

v2 = c.always42
assert v2 == 42

# Works in expressions too.
assert c.diameter + 1 == 11
assert c.always42 == 42
