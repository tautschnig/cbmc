# PLR 10.6.2: MatchClass keyword patterns.
# case Cls(attr=pattern) — isinstance check plus
# named-attribute binding via recursive compile_pattern.


class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y


def classify(p: Point) -> int:
    match p:
        case Point(x=0, y=0):
            return 1
        case Point(x=0):
            return 2
        case Point():
            return 3


assert classify(Point(0, 0)) == 1
assert classify(Point(0, 5)) == 2
assert classify(Point(5, 5)) == 3


class Box:
    def __init__(self, w: int, h: int) -> None:
        self.w = w
        self.h = h


# isinstance check: a Box passes Box(), fails Point().
def describe(p) -> int:
    match p:
        case Point(x=0):
            return 10
        case Box():
            return 20
        case _:
            return 30


assert describe(Box(1, 2)) == 20
assert describe(Point(0, 5)) == 10
