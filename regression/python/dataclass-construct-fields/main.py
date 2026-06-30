# A @dataclass with no explicit __init__ synthesizes one that binds constructor
# args to the annotated fields in order (positional + keyword), respecting
# defaults. Field reads after construction must return the passed values.
from dataclasses import dataclass


@dataclass
class Point:
    x: int
    y: int
    z: int = 9


def main() -> None:
    p = Point(1, 2)
    assert p.x == 1 and p.y == 2 and p.z == 9
    q = Point(y=20, x=10, z=30)
    assert q.x == 10 and q.y == 20 and q.z == 30


main()
