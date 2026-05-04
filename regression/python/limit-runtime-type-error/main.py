# Missing TypeError detection for incompatible operations
x: int = 1
y: str = "hello"
z = x + y  # Should raise TypeError
assert False  # Should be unreachable
