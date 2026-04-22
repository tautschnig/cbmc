class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y

p = Point(3, 4)
assert p.x == 3
assert p.y == 4
