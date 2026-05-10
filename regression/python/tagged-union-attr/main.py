# Read/write attributes on tagged-union (CLASS) values.
# Previously x.attr returned nondet for a tagged-union base
# even when a unique matching class existed.


class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y


def mk(flag: bool):
    if flag:
        return Point(1, 2)
    return Point(3, 4)


p = mk(True)
# p is tagged-union. .x should read through __class_ptr.
assert p.x == 1 or p.x == 3
assert p.y == 2 or p.y == 4

# Attribute write through tagged-union:
p.x = 99
assert p.x == 99
