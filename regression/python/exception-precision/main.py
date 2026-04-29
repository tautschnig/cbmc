def risky(x: int) -> int:
    if x == 0:
        raise ValueError("zero")
    return x

try:
    result: int = risky(0)
except ValueError:
    result = -1
assert result == -1
