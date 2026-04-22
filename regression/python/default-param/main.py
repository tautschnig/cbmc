def add(a: int, b: int = 10) -> int:
    return a + b

x: int = add(5)
assert x == 15
y: int = add(5, 20)
assert y == 25
