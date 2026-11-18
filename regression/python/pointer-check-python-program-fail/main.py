# Lists, instances and instances stored as class attributes under
# --pointer-check.
class Cell:
    def __init__(self, v: int) -> None:
        self.v = v


class Grid:
    origin = Cell(4)


def total(xs: list[int]) -> int:
    s = 0
    for x in xs:
        s = s + x
    return s


c = Cell(2)
o = Grid.origin
assert total([c.v, 4]) == 7
