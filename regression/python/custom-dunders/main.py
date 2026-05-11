# Custom __eq__ / __lt__ / __le__ / __gt__ / __ge__ dunders
# on class instances (PLR 3.3.1 / 3.3.8).
#
# When a class defines these methods, comparison operators
# on instances route through them instead of using CBMC's
# default struct comparison.


class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y

    def __eq__(self, other: "Point") -> bool:
        return self.x == other.x and self.y == other.y

    def __lt__(self, other: "Point") -> bool:
        return self.x < other.x or (self.x == other.x and self.y < other.y)

    def __le__(self, other: "Point") -> bool:
        return self.x < other.x or (self.x == other.x and self.y <= other.y)


a = Point(1, 2)
b = Point(1, 2)
c = Point(3, 4)

# __eq__
assert a == b
assert not (a == c)

# __lt__
assert a < c
assert not (c < a)
assert not (a < b)  # equal points are not <

# __le__
assert a <= b  # equal points are <=
assert a <= c
assert not (c <= a)


# Non-dunder class: structural equality still works.
class Plain:
    def __init__(self, v: int) -> None:
        self.v = v


p1 = Plain(5)
p2 = Plain(5)
assert p1 == p2  # same fields
