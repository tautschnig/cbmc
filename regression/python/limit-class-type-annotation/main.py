# PLR §4.7.2: type annotations with user-defined types
class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x: int = x
        self.y: int = y

def distance(p: Point) -> int:
    return p.x + p.y

pt: Point = Point(3, 4)
assert distance(pt) == 7
