class Cell:
    def __init__(self, v: int) -> None:
        self.v = v


class Grid:
    origin = Cell(4)


g = Grid()
assert g.origin.v == 4
Grid.origin.v = 9
assert g.origin.v == 9
h = Grid()
assert h.origin.v == 4
