class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y

def distance(p: Point) -> int:
    return p.x + p.y

p = Point(3, 4)
assert distance(p) == 7
