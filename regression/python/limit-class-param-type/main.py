# PLR §4.7.2: class type in function parameter
class Point:
    def __init__(self, x: int) -> None:
        self.x: int = x

def get_x(p: Point) -> int:
    return p.x

assert get_x(Point(5)) == 5
