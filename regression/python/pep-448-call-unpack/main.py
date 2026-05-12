# PEP 448: iterable unpacking in function calls.
# f(*c) spreads c's elements as positional args.

def add3(x: int, y: int, z: int) -> int:
    return x + y + z


c = [1, 2, 3]
assert add3(*c) == 6

# Mixed with explicit args
def add4(x: int, y: int, z: int, w: int) -> int:
    return x + y + z + w


assert add4(0, *c) == 6
assert add4(*c, 10) == 16
